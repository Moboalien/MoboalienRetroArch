/*
 * retro-handoff: portable "screen producer" module.
 *
 * A host app (RetroArch, or any other emulator/GL app) registers a Surface
 * handed over by an external encoder owner (e.g. the A2AStreaming APK).  While
 * a surface is attached the host's graphics context is expected to render into
 * the ANativeWindow obtained from handoff_window() instead of its own UI
 * window.  With no surface attached the module is inert: the host behaves
 * exactly as before.
 *
 * The module has no dependencies on the host, only on Bionic / Android NDK
 * headers, so it can be copied into any app.  Host-specific behaviour is
 * injected through handoff_set_host_control().
 */
#ifndef RETRO_HANDOFF_SURFACE_H
#define RETRO_HANDOFF_SURFACE_H

#include <android/native_window.h>
#include <jni.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True while an encoder-surface is attached. */
bool handoff_active(void);

/* The injected window, or NULL when inactive. */
ANativeWindow *handoff_window(void);

/* (Re)inject a native window acquired from a handed-over Surface. */
void handoff_set_surface(ANativeWindow *window);

/* Detach and drop the injected window. */
void handoff_clear_surface(void);

/* Optional: fired whenever the attached surface changes state. */
void handoff_set_changed_cb(void (*cb)(void));

/* Video-resync request, primarily for the host's main-thread context glue.
 * handoff_request_resync() is safe to call from any thread (e.g. a binder
 * service thread); handoff_consume_resync_request() returns true exactly once
 * and is meant to be polled once per rendered frame on the host's graphics
 * thread so the EGL surface can be swapped without racing the render loop.
 */
void handoff_request_resync(void);
bool handoff_consume_resync_request(void);

/* Host-provided emulation control, wired by the embedding app. */
typedef void (*handoff_control_fn)(void);
void handoff_set_host_control(handoff_control_fn start, handoff_control_fn stop);

/* Host-provided liveness probe: true when the host's native graphics
 * context already exists (a real activity has started at least once), so a
 * client embedding can decide whether it needs to cold-boot the host. */
typedef bool (*handoff_alive_fn)(void);
void handoff_set_host_alive(handoff_alive_fn alive);
bool handoff_host_alive(void);

/* JNI entry points matching com.a2a.streaming.retro.RetroHandoffNative.
 * Static-convention Java_..._nativeXXX symbols so the VM auto-binds them on
 * System.loadLibrary without needing a module-owned JNI_OnLoad (the host lib
 * may already export one). Safe to call from the service thread; attach/detach
 * are synchronous and idempotent. */
jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeAttachSurface(
      JNIEnv *env, jobject thiz, jobject jsurface);
void     Java_com_a2a_streaming_retro_RetroHandoffNative_nativeDetachSurface(
      JNIEnv *env, jobject thiz);
jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeStartEmulation(
      JNIEnv *env, jobject thiz);
void     Java_com_a2a_streaming_retro_RetroHandoffNative_nativeStopEmulation(
      JNIEnv *env, jobject thiz);
jboolean Java_com_a2a_streaming_retro_RetroHandoffNative_nativeIsProducerAlive(
      JNIEnv *env, jobject thiz);

#ifdef __cplusplus
}
#endif

#endif /* RETRO_HANDOFF_SURFACE_H */