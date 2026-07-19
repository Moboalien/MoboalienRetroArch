package java_client.h264_stream_client;

import org.bytedeco.ffmpeg.avutil.AVFrame;
import org.bytedeco.javacpp.BytePointer;

import javax.swing.*;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferByte;

import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_NV12;
import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_YUV420P;

public class SwingVideoRenderer implements VideoRenderer {
    private final VideoPanel panel = new VideoPanel();
    private BufferedImage reusableImage;
    private byte[] reusableImageData;
    private int width = 0;
    private int height = 0;

    @Override
    public Component getComponent() {
        return panel;
    }

    @Override
    public void init(int width, int height) {
        if (width <= 0 || height <= 0) return;
        this.width = width;
        this.height = height;
        reusableImage = new BufferedImage(width, height, BufferedImage.TYPE_3BYTE_BGR);
        reusableImageData = ((DataBufferByte) reusableImage.getRaster().getDataBuffer()).getData();
        panel.setPreferredSize(new Dimension(width, height));
    }

    @Override
    public void renderFrame(AVFrame frame) {
        if (frame == null) return;
        int w = frame.width();
        int h = frame.height();
        if (w <= 0 || h <= 0) return;
        if (reusableImage == null || reusableImage.getWidth() != w || reusableImage.getHeight() != h) {
            init(w, h);
        }

        int pixFmt = frame.format();
        if (pixFmt == AV_PIX_FMT_NV12) {
            convertNV12ToBGR(frame, w, h, reusableImageData);
        } else if (pixFmt == AV_PIX_FMT_YUV420P) {
            convertYUV420PToBGR(frame, w, h, reusableImageData);
        } else {
            // Unsupported format for this renderer: drop the frame
            return;
        }
        panel.updateFrame(reusableImage);
    }

    @Override
    public void stop() {
        // no resources to free in this simple Swing renderer
    }

    private class VideoPanel extends JPanel {
        private volatile BufferedImage currentFrame;

        public void updateFrame(BufferedImage frame) {
            this.currentFrame = frame;
            // repaint on EDT
            SwingUtilities.invokeLater(this::repaint);
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            g.setColor(Color.BLACK);
            g.fillRect(0, 0, getWidth(), getHeight());

            BufferedImage frame = this.currentFrame;
            if (frame != null) {
                int panelW = getWidth(), panelH = getHeight();
                int imgW = frame.getWidth(), imgH = frame.getHeight();

                Graphics2D g2d = (Graphics2D) g;
                g2d.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_SPEED);
                g2d.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR);
                g2d.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_OFF);

                double scale = Math.min((double) panelW / imgW, (double) panelH / imgH);
                int scaledW = (int) (imgW * scale);
                int scaledH = (int) (imgH * scale);
                int x = (panelW - scaledW) / 2;
                int y = (panelH - scaledH) / 2;
                g2d.drawImage(frame, x, y, scaledW, scaledH, null);
            } else {
                g.setColor(Color.WHITE);
                g.drawString("Waiting for H.264 stream...", 50, 50);
            }
        }
    }

    // --- YUV -> BGR conversions (port of previous implementation) ---
    private void convertNV12ToBGR(AVFrame frame, int width, int height, byte[] out) {
        BytePointer yPtr = frame.data(0);
        BytePointer uvPtr = frame.data(1);
        int yStride = frame.linesize(0);
        int uvStride = frame.linesize(1);

        byte[] y = new byte[width * height];
        byte[] uv = new byte[width * (height / 2)];

        for (int r = 0; r < height; r++) {
            yPtr.position(r * yStride);
            yPtr.get(y, r * width, width);
        }
        for (int r = 0; r < height / 2; r++) {
            uvPtr.position(r * uvStride);
            uvPtr.get(uv, r * width, width);
        }

        int outIndex = 0;
        for (int row = 0; row < height; row++) {
            int uvRow = (row / 2) * width;
            for (int col = 0; col < width; col++) {
                int Y = y[row * width + col] & 0xFF;
                int uvIndex = uvRow + (col & ~1);
                int U = uv[uvIndex] & 0xFF;
                int V = uv[uvIndex + 1] & 0xFF;

                int c = Y - 16;
                int d = U - 128;
                int e = V - 128;
                int rVal = (298 * c + 409 * e + 128) >> 8;
                int gVal = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int bVal = (298 * c + 516 * d + 128) >> 8;
                if (rVal < 0) rVal = 0; else if (rVal > 255) rVal = 255;
                if (gVal < 0) gVal = 0; else if (gVal > 255) gVal = 255;
                if (bVal < 0) bVal = 0; else if (bVal > 255) bVal = 255;

                out[outIndex++] = (byte) bVal;
                out[outIndex++] = (byte) gVal;
                out[outIndex++] = (byte) rVal;
            }
        }
    }

    private void convertYUV420PToBGR(AVFrame frame, int width, int height, byte[] out) {
        BytePointer yPtr = frame.data(0);
        BytePointer uPtr = frame.data(1);
        BytePointer vPtr = frame.data(2);
        int yStride = frame.linesize(0);
        int uStride = frame.linesize(1);
        int vStride = frame.linesize(2);

        byte[] y = new byte[width * height];
        byte[] u = new byte[(width / 2) * (height / 2)];
        byte[] v = new byte[(width / 2) * (height / 2)];

        for (int r = 0; r < height; r++) {
            yPtr.position(r * yStride);
            yPtr.get(y, r * width, width);
        }
        for (int r = 0; r < height / 2; r++) {
            uPtr.position(r * uStride);
            uPtr.get(u, r * (width / 2), width / 2);
            vPtr.position(r * vStride);
            vPtr.get(v, r * (width / 2), width / 2);
        }

        int outIndex = 0;
        for (int row = 0; row < height; row++) {
            int uRow = (row / 2) * (width / 2);
            for (int col = 0; col < width; col++) {
                int Y = y[row * width + col] & 0xFF;
                int U = u[uRow + (col / 2)] & 0xFF;
                int V = v[uRow + (col / 2)] & 0xFF;

                int c = Y - 16;
                int d = U - 128;
                int e = V - 128;
                int rVal = (298 * c + 409 * e + 128) >> 8;
                int gVal = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int bVal = (298 * c + 516 * d + 128) >> 8;
                if (rVal < 0) rVal = 0; else if (rVal > 255) rVal = 255;
                if (gVal < 0) gVal = 0; else if (gVal > 255) gVal = 255;
                if (bVal < 0) bVal = 0; else if (bVal > 255) bVal = 255;

                out[outIndex++] = (byte) bVal;
                out[outIndex++] = (byte) gVal;
                out[outIndex++] = (byte) rVal;
            }
        }
    }
}
