/*
 * retro-handoff: portable "audio sink" producer module (PCM half).
 *
 * Mirrors handoff_surface.c for audio. The encoder owner (A2AStreaming) creates
 * a shared-memory ring buffer and hands its fd to the host over AIDL
 * (IRetroStreamService.attachAudioSink). This module maps that fd and pushes
 * 16-bit stereo PCM produced by the host's audio driver into the ring; the
 * owner's AAC encoder drains it. With no ring attached the module is inert: the
 * host behaves exactly as before.
 *
 * The module has no dependencies on the host, only on Bionic / Android NDK
 * headers, so it can be copied into any app. The producer hook is exposed as a
 * strong symbol overriding the opensl driver's weak reference so RetroArch's
 * core audio sources never need to know this module exists.
 *
 * Ring layout (first 64 bytes, native little-endian; values are aligned so the
 * long write/read offsets are naturally atomic on ARM64):
 *
 *   +0   u32 magic       0x41554441 ("AUDA")
 *   +4   u32 version     1
 *   +8   u32 sampleRate  0 until the driver first streams audio
 *   +12  u32 channels    2
 *   +16  u32 frameBytes  4 (stereo s16)
 *   +20  u32 state       0 idle, 1 running
 *   +24  u32 reserved
 *   +28  u32 reserved
 *   +32  u64 writeOffset producer's total bytes published (monotonic)
 *   +40  u64 readOffset  consumer's total bytes consumed (monotonic)
 *   +48  u32 reserved x4
 *   +64  ring data ...
 *
 * Keep the header offsets in lockstep with the Java consumer
 * (com.a2a.streaming.RemoteAudioSource).
 */
#ifndef RETRO_HANDOFF_AUDIO_H
#define RETRO_HANDOFF_AUDIO_H

#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HANDOFF_AUDIO_HEADER_BYTES 64u
#define HANDOFF_AUDIO_MAGIC        0x41554441u /* "AUDA" little-endian */
#define HANDOFF_AUDIO_VERSION      1u

/* Header field offsets. */
#define HO_A_OFF_MAGIC           0u
#define HO_A_OFF_VERSION         4u
#define HO_A_OFF_SAMPLE_RATE     8u
#define HO_A_OFF_CHANNELS        12u
#define HO_A_OFF_FRAME_BYTES     16u
#define HO_A_OFF_STATE           20u
#define HO_A_OFF_WRITE_OFFSET    32u
#define HO_A_OFF_READ_OFFSET     40u

#define HO_A_STATE_IDLE          0u
#define HO_A_STATE_RUNNING       1u

typedef struct handoff_audio_header
{
   uint32_t magic;
   uint32_t version;
   uint32_t sample_rate;
   uint32_t channel_count;
   uint32_t frame_bytes;
   uint32_t state;
   uint32_t reserved0;
   uint32_t reserved1;
   uint64_t write_offset;
   uint64_t read_offset;
   uint32_t reserved2[4];
} handoff_audio_header;

/* Map a shared-memory producer ring from an fd received over AIDL
 * (capacity_bytes of PCM payload after the 64-byte header). The fd is mmap'd
 * immediately and may be closed by the caller afterwards. Returns false if the
 * mapping fails. Idempotent: a previously attached ring is detached first. */
bool handoff_audio_attach(int fd, uint32_t capacity_bytes);

/* Unmap the ring and mark it idle (in the mapped header) so the consumer can
 * notice teardown. Idempotent. */
void handoff_audio_detach(void);

/* True while a ring is attached. */
bool handoff_audio_active(void);

/* Push one chunk of 16-bit stereo PCM into the attached ring. Never blocks:
 * the chunk is dropped if the consumer has not drained enough room, and the
 * sample rate / running state are published lazily on the first chunk. Safe to
 * call from a realtime audio thread. No-op when no ring is attached. */
void handoff_audio_submit(unsigned rate, const int16_t *samples, size_t frames);

/* JNI entry points matching com.a2a.streaming.retro.RetroHandoffNative. */
jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeAttachAudioSink(
      JNIEnv *env, jobject thiz, jint jfd, jint capacity);
void     Java_com_a2a_streaming_retro_RetroHandoffNative_nativeDetachAudioSink(
      JNIEnv *env, jobject thiz);
jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeIsAudioCaptureActive(
      JNIEnv *env, jobject thiz);

#ifdef __cplusplus
}
#endif

#endif /* RETRO_HANDOFF_AUDIO_H */