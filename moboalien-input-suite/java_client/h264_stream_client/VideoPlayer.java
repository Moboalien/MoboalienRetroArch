package java_client.h264_stream_client;

import java.awt.image.BufferedImage;

public interface VideoPlayer {
    void initialize() throws Exception;
    void feedFrame(byte[] frameData, boolean isConfig, boolean isKeyFrame, long pts);
    void start();
    void stop();
    void setFrameCallback(FrameCallback callback);
    
    interface FrameCallback {
        void onFrameDecoded(BufferedImage image, int frameNumber);
        void onError(Exception e);
    }
}