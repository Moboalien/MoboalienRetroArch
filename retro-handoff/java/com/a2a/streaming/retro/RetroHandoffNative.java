package com.a2a.streaming.retro;

import android.view.Surface;

/**
 * Portable JNI bridge for the retro-handoff module. The backing library is
 * loaded with the name set by {@link #init(String)}; the embedding app must
 * call {@code RetroHandoffNative.init(BuildConfig.xxx)} (or the default is
 * used) before any handle-off operation so the same module can ship inside any
 * APK without renaming the JNI symbols.
 */
public final class RetroHandoffNative {

    private static final String DEFAULT_LIBRARY = "handoff";

    private static volatile String libraryName = DEFAULT_LIBRARY;
    private static boolean loaded = false;

    private RetroHandoffNative() {
    }

    public static synchronized void init(String libName) {
        if (loaded) {
            return;
        }
        if (libName == null || libName.isEmpty()) {
            libName = DEFAULT_LIBRARY;
        }
        libraryName = libName;
        System.loadLibrary(libName);
        loaded = true;
    }

    private static String lib() {
        init(null);
        return libraryName;
    }

    /** Attaches EGL to the handed-over Surface. Returns false on failure. */
    public static boolean attachEncoderSurface(Surface surface) {
        lib();
        return nativeAttachSurface(surface);
    }

    public static void detachEncoderSurface() {
        lib();
        nativeDetachSurface();
    }

    public static boolean startEmulation() {
        lib();
        return nativeStartEmulation();
    }

    public static void stopEmulation() {
        lib();
        nativeStopEmulation();
    }

    /** True when the host's native GL context already exists (activity started
     *  at least once). Callers may use this to skip cold-booting RetroArch. */
    public static boolean isProducerAlive() {
        lib();
        return nativeIsProducerAlive();
    }

    /** Maps the shared-memory audio ring passed over AIDL and starts capturing
     *  PCM from the host audio driver into it. Returns false on failure. */
    public static boolean attachAudioSink(int fd, int capacityBytes) {
        lib();
        return nativeAttachAudioSink(fd, capacityBytes);
    }

    /** Unmaps the audio ring and stops PCM capture. */
    public static void detachAudioSink() {
        lib();
        nativeDetachAudioSink();
    }

    /** True while an audio ring is attached. */
    public static boolean isAudioCaptureActive() {
        lib();
        return nativeIsAudioCaptureActive();
    }

    private static native boolean nativeAttachSurface(Surface surface);

    private static native void nativeDetachSurface();

    private static native boolean nativeStartEmulation();

    private static native void nativeStopEmulation();

    private static native boolean nativeIsProducerAlive();

    private static native boolean nativeAttachAudioSink(int fd, int capacityBytes);

    private static native void nativeDetachAudioSink();

    private static native boolean nativeIsAudioCaptureActive();
}