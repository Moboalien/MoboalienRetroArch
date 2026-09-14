/*
 * retro-handoff: portable "screen producer" module implementation.
 * See handoff_surface.h.
 */
#include "handoff_surface.h"

#include <android/native_window_jni.h>
#include <pthread.h>
#include <android/log.h>

#define LOG_TAG "RetroHandoff"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static ANativeWindow      *s_window   = NULL;
static void              (*s_changed)(void) = NULL;
static handoff_control_fn  s_host_start = NULL;
static handoff_control_fn  s_host_stop  = NULL;
static handoff_alive_fn    s_host_alive = NULL;
static bool                s_resync    = false;
static bool                s_reset      = false;

void handoff_request_resync(void)
{
   pthread_mutex_lock(&s_mutex);
   s_resync = true;
   pthread_mutex_unlock(&s_mutex);
}

bool handoff_consume_resync_request(void)
{
   bool r;
   pthread_mutex_lock(&s_mutex);
   r = s_resync;
   s_resync = false;
   pthread_mutex_unlock(&s_mutex);
   return r;
}

void handoff_request_reset(void)
{
   pthread_mutex_lock(&s_mutex);
   s_reset = true;
   pthread_mutex_unlock(&s_mutex);
}

bool handoff_pending_reset(void)
{
   bool r;
   pthread_mutex_lock(&s_mutex);
   r = s_reset;
   pthread_mutex_unlock(&s_mutex);
   return r;
}

void handoff_clear_reset(void)
{
   pthread_mutex_lock(&s_mutex);
   s_reset = false;
   pthread_mutex_unlock(&s_mutex);
}

bool handoff_active(void)
{
   bool active;
   pthread_mutex_lock(&s_mutex);
   active = (s_window != NULL);
   pthread_mutex_unlock(&s_mutex);
   return active;
}

ANativeWindow *handoff_window(void)
{
   ANativeWindow *w;
   pthread_mutex_lock(&s_mutex);
   w = s_window;
   if (w)
      ANativeWindow_acquire(w);
   pthread_mutex_unlock(&s_mutex);
   return w;
}

void handoff_set_surface(ANativeWindow *window)
{
   ANativeWindow *old = NULL;
   void (*cb)(void)     = NULL;

   pthread_mutex_lock(&s_mutex);
   old  = s_window;
   s_window = window;          /* ownership moves to the module */
   cb   = s_changed;
   pthread_mutex_unlock(&s_mutex);

   if (old)
      ANativeWindow_release(old);

   handoff_request_resync();

   if (cb)
      cb();
}

void handoff_clear_surface(void)
{
   handoff_set_surface(NULL);
}

void handoff_set_changed_cb(void (*cb)(void))
{
   pthread_mutex_lock(&s_mutex);
   s_changed = cb;
   pthread_mutex_unlock(&s_mutex);
}

void handoff_set_host_control(handoff_control_fn start, handoff_control_fn stop)
{
   pthread_mutex_lock(&s_mutex);
   s_host_start = start;
   s_host_stop  = stop;
   pthread_mutex_unlock(&s_mutex);
}

void handoff_set_host_alive(handoff_alive_fn alive)
{
   pthread_mutex_lock(&s_mutex);
   s_host_alive = alive;
   pthread_mutex_unlock(&s_mutex);
}

bool handoff_host_alive(void)
{
   bool alive;
   pthread_mutex_lock(&s_mutex);
   alive = s_host_alive ? s_host_alive() : false;
   pthread_mutex_unlock(&s_mutex);
   return alive;
}

static handoff_control_fn get_host(handoff_control_fn *slot)
{
   handoff_control_fn fn;
   pthread_mutex_lock(&s_mutex);
   fn = *slot;
   pthread_mutex_unlock(&s_mutex);
   return fn;
}

/*
 * JNI surface: handoff_window() keeps a reference, so the module owns the
 * ANativeWindow.  We must not hold the Java Surface itself past this call.
 */
static jboolean attach_surface_from_jni(JNIEnv *env, jobject jsurface)
{
   ANativeWindow *window = ANativeWindow_fromSurface(env, jsurface);
   if (!window)
   {
      LOGE("ANativeWindow_fromSurface failed");
      return JNI_FALSE;
   }
   handoff_set_surface(window);
   return JNI_TRUE;
}

jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeAttachSurface(
      JNIEnv *env, jobject thiz, jobject jsurface)
{
   if (!jsurface)
      return JNI_FALSE;
   return attach_surface_from_jni(env, jsurface);
}

void Java_com_a2a_streaming_retro_RetroHandoffNative_nativeDetachSurface(
      JNIEnv *env, jobject thiz)
{
   (void)env; (void)thiz;
   handoff_clear_surface();
}

jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeStartEmulation(
      JNIEnv *env, jobject thiz)
{
   handoff_control_fn fn = get_host(&s_host_start);
   if (!fn)
      return JNI_FALSE;
   fn();
   return JNI_TRUE;
}

void Java_com_a2a_streaming_retro_RetroHandoffNative_nativeStopEmulation(
      JNIEnv *env, jobject thiz)
{
   handoff_control_fn fn = get_host(&s_host_stop);
   if (fn)
      fn();
}

jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeIsProducerAlive(
      JNIEnv *env, jobject thiz)
{
   (void)env; (void)thiz;
   return handoff_host_alive() ? JNI_TRUE : JNI_FALSE;
}