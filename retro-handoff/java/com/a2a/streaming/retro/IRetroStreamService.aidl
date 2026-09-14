// retro-handoff shared contract: the encoder owner (A2AStreaming) hands its
// codec input Surface to the screen-producer app, then starts/stops its
// emulation. Audio mirror: the owner creates a shared-memory ring, passes the
// mapped fd, and the producer writes 16-bit stereo PCM into it. Both ends must
// ship an identical copy of this file (same package and method signatures) so
// the auto-generated Binder stubs line up.
package com.a2a.streaming.retro;

interface IRetroStreamService {
    void attachEncoderSurface(in android.view.Surface surface);
    void detachEncoderSurface();
    void startEmulation();
    void stopEmulation();
    boolean isProducerAlive();
    boolean isStreaming();

    // Audio capture: the owner creates a SharedMemory ring of capacityBytes
    // payload (64-byte header + PCM data) and passes its fd. The producer maps
    // it and writes 16-bit stereo PCM. The fd is consumed during the call.
    void attachAudioSink(in android.os.ParcelFileDescriptor fd, int capacityBytes);
    void detachAudioSink();
    boolean isAudioStreaming();
}