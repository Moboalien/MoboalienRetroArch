// retro-handoff shared contract: the encoder owner (A2AStreaming) hands its
// codec input Surface to the screen-producer app, then starts/stops its
// emulation. Both ends must ship an identical copy of this file (same package
// and method signatures) so the auto-generated Binder stubs line up.
package com.a2a.streaming.retro;

interface IRetroStreamService {
    void attachEncoderSurface(in android.view.Surface surface);
    void detachEncoderSurface();
    void startEmulation();
    void stopEmulation();
    boolean isProducerAlive();
    boolean isStreaming();
}