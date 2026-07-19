package java_client.h264_stream_client;

import org.bytedeco.ffmpeg.avutil.AVFrame;
import java.awt.Component;

public interface VideoRenderer {
    /**
     * The component to add into the UI (Swing/AWT component)
     */
    Component getComponent();

    /**
     * Initialize or reinitialize for given frame size.
     */
    void init(int width, int height);

    /**
     * Render an AVFrame (may be NV12, YUV420P or others).
     * Implementations should be resilient and drop unsupported frames.
     */
    void renderFrame(AVFrame frame);

    /**
     * Stop and release any resources.
     */
    void stop();
}
