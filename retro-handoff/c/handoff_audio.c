/*
 * retro-handoff: portable "audio sink" producer module implementation.
 * See handoff_audio.h.
 */
#include "handoff_audio.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

#include <android/log.h>

#define LOG_TAG "RetroHandoffAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint8_t               *s_map       = NULL;
static size_t                 s_map_size  = 0;
static size_t                 s_capacity  = 0; /* ring payload bytes */
static handoff_audio_header  *s_h         = NULL;
static uint8_t               *s_ring      = NULL;
static bool                   s_started   = false;

static void handoff_audio_detach_locked(void)
{
   if (s_h)
   {
      /* Publish idle before unmapping so the consumer notices teardown evenly. */
      s_h->state = HO_A_STATE_IDLE;
      __sync_synchronize();
   }
   if (s_map && s_map_size)
      munmap(s_map, s_map_size);

   s_map      = NULL;
   s_map_size = 0;
   s_capacity = 0;
   s_h        = NULL;
   s_ring     = NULL;
   s_started  = false;
}

void handoff_audio_detach(void)
{
   pthread_mutex_lock(&s_mutex);
   handoff_audio_detach_locked();
   pthread_mutex_unlock(&s_mutex);
}

bool handoff_audio_attach(int fd, uint32_t capacity_bytes)
{
   uint8_t              *m;
   handoff_audio_header *h;
   size_t                total;

   if (fd < 0)
      return false;

   total = HANDOFF_AUDIO_HEADER_BYTES + capacity_bytes;
   m = (uint8_t*)mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (m == MAP_FAILED)
   {
      LOGE("attach: mmap failed (fd=%d, errno=%d)", fd, errno);
      return false;
   }
   h = (handoff_audio_header*)m;

   pthread_mutex_lock(&s_mutex);
   handoff_audio_detach_locked();

   s_map      = m;
   s_map_size = total;
   s_capacity = capacity_bytes;
   s_h        = h;
   s_ring     = m + HANDOFF_AUDIO_HEADER_BYTES;

   h->magic         = HANDOFF_AUDIO_MAGIC;
   h->version       = HANDOFF_AUDIO_VERSION;
   h->sample_rate   = 0; /* published on the first push */
   h->state         = HO_A_STATE_IDLE;
   h->channel_count = 2;
   h->frame_bytes   = 4; /* stereo s16 */
   h->write_offset  = 0;
   h->read_offset   = 0;
   __sync_synchronize();
   pthread_mutex_unlock(&s_mutex);

   LOGI("audio ring attached: capacity=%u bytes", capacity_bytes);
   return true;
}

bool handoff_audio_active(void)
{
   bool active;
   pthread_mutex_lock(&s_mutex);
   active = (s_h != NULL);
   pthread_mutex_unlock(&s_mutex);
   return active;
}

void handoff_audio_submit(unsigned rate, const int16_t *samples, size_t frames)
{
   handoff_audio_header *h;
   uint8_t              *ring;
   size_t                capacity;
   size_t                bytes;
   size_t                write_pos;
   size_t                first;
   uint64_t              written;
   uint64_t              read;

   if (!samples || !frames)
      return;

   pthread_mutex_lock(&s_mutex);
   h        = s_h;
   ring     = s_ring;
   capacity = s_capacity;
   if (!h || !ring || capacity == 0)
   {
      pthread_mutex_unlock(&s_mutex);
      return;
   }

   bytes = frames * 4; /* stereo s16 */

   written = __atomic_load_n(&h->write_offset, __ATOMIC_ACQUIRE);

   /* Publish the stream format and running state before any data so the
    * consumer can configure its encoder. Cheap enough to check every chunk. */
   if (!s_started || h->state != HO_A_STATE_RUNNING || h->sample_rate != rate)
   {
      h->sample_rate = rate;
      h->channel_count = 2;
      h->frame_bytes   = 4;
      __sync_synchronize();
      h->state = HO_A_STATE_RUNNING;
      __sync_synchronize();
      s_started = true;
   }

   read = __atomic_load_n(&h->read_offset, __ATOMIC_ACQUIRE);

   /* Ring is full (consumer hasn't drained) or this chunk just does not fit:
    * drop it rather than stall the audio thread. */
   if (written - read > capacity || bytes > capacity - (size_t)(written - read))
   {
      pthread_mutex_unlock(&s_mutex);
      return;
   }

   write_pos = (size_t)(written % capacity);
   first     = capacity - write_pos;
   if (first > bytes)
      first = bytes;
   memcpy(ring + write_pos, samples, first);
   if (first < bytes)
      memcpy(ring, (const uint8_t*)samples + first, bytes - first);

   __atomic_store_n(&h->write_offset, written + bytes, __ATOMIC_RELEASE);
   pthread_mutex_unlock(&s_mutex);
}

/* ---- JNI bridge (mirrors the surface module's static-convention entries) ---- */

jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeAttachAudioSink(
      JNIEnv *env, jobject thiz, jint jfd, jint capacity)
{
   (void)env; (void)thiz;
   if (jfd < 0 || capacity < 4096)
   {
      LOGE("attach: bad args fd=%d capacity=%d", (int)jfd, (int)capacity);
      return JNI_FALSE;
   }
   return handoff_audio_attach((int)jfd, (uint32_t)capacity) ? JNI_TRUE : JNI_FALSE;
}

void Java_com_a2a_streaming_retro_RetroHandoffNative_nativeDetachAudioSink(
      JNIEnv *env, jobject thiz)
{
   (void)env; (void)thiz;
   handoff_audio_detach();
}

jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeIsAudioCaptureActive(
      JNIEnv *env, jobject thiz)
{
   (void)env; (void)thiz;
   return handoff_audio_active() ? JNI_TRUE : JNI_FALSE;
}