package java_client.h264_stream_client;

import org.bytedeco.ffmpeg.avutil.AVFrame;
import org.bytedeco.javacpp.BytePointer;

import javax.swing.*;
import java.awt.*;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicReference;

import javafx.application.Platform;
import javafx.embed.swing.JFXPanel;
import javafx.scene.Scene;
import javafx.scene.image.ImageView;
import javafx.scene.image.PixelFormat;
import javafx.scene.image.PixelWriter;
import javafx.scene.image.WritableImage;
import javafx.scene.layout.StackPane;

import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_NV12;
import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_YUV420P;

public class JavaFXYuvRenderer implements VideoRenderer {
    private final JFXPanel fxPanel = new JFXPanel();
    private WritableImage writableImage;
    private PixelWriter pixelWriter;
    private PixelFormat<ByteBuffer> pixelFormat = PixelFormat.getByteRgbInstance();

    private ByteBuffer rgbBuffer;
    private byte[] yPlane;
    private byte[] uvPlane;
    private byte[] uPlane;
    private byte[] vPlane;

    private int width;
    private int height;

    private final AtomicReference<ImageView> imageViewRef = new AtomicReference<>();

    public JavaFXYuvRenderer() {
        // Constructing a JFXPanel initializes the JavaFX runtime on first use.
        Platform.runLater(() -> {
            ImageView iv = new ImageView();
            iv.setPreserveRatio(true);
            StackPane root = new StackPane(iv);
            Scene scene = new Scene(root);
            fxPanel.setScene(scene);
            imageViewRef.set(iv);
            // Bind ImageView fit size to the root to avoid cropping when panel resizes
            iv.fitWidthProperty().bind(root.widthProperty());
            iv.fitHeightProperty().bind(root.heightProperty());
        });
    }

    @Override
    public Component getComponent() {
        return fxPanel;
    }

    @Override
    public void init(int width, int height) {
        if (width <= 0 || height <= 0) return;
        this.width = width;
        this.height = height;
        int rgbSize = width * height * 3;
        rgbBuffer = ByteBuffer.allocateDirect(rgbSize);
        yPlane = new byte[width * height];
        uvPlane = new byte[width * (height / 2)];
        uPlane = new byte[(width / 2) * (height / 2)];
        vPlane = new byte[(width / 2) * (height / 2)];

        // Ensure Swing component has a preferred size so layout gives it space
        SwingUtilities.invokeLater(() -> {
            fxPanel.setPreferredSize(new Dimension(width, height));
            fxPanel.revalidate();
        });

        Platform.runLater(() -> {
            writableImage = new WritableImage(width, height);
            pixelWriter = writableImage.getPixelWriter();
            ImageView iv = imageViewRef.get();
            if (iv != null) {
                iv.setImage(writableImage);
                // fitWidth/fitHeight are bound to parent; no fixed size needed
            }
        });
    }

    @Override
    public void renderFrame(AVFrame frame) {
        if (frame == null) return;
        int w = frame.width();
        int h = frame.height();
        if (w <= 0 || h <= 0) return;
        if (w != width || h != height) {
            init(w, h);
        }

        int pixFmt = frame.format();
        boolean converted = false;
        if (pixFmt == AV_PIX_FMT_NV12) {
            converted = convertNV12ToRGB(frame, w, h, rgbBuffer);
        } else if (pixFmt == AV_PIX_FMT_YUV420P) {
            converted = convertYUV420PToRGB(frame, w, h, rgbBuffer);
        } else {
            // unsupported, drop
            return;
        }

        if (!converted) return;

        // Push rgbBuffer into the JavaFX image on FX thread
        final ByteBuffer fb = rgbBuffer.asReadOnlyBuffer();
        fb.rewind();
        Platform.runLater(() -> {
            if (pixelWriter != null) {
                pixelWriter.setPixels(0, 0, width, height, pixelFormat, fb, width * 3);
            }
        });
    }

    @Override
    public void stop() {
        Platform.runLater(() -> {
            ImageView iv = imageViewRef.get();
            if (iv != null) iv.setImage(null);
        });
    }

    /**
     * Convert NV12 planes into RGB ByteBuffer (RGB24 interleaved B,G,R order expected by PixelFormat.getByteRgbInstance())
     */
    private boolean convertNV12ToRGB(AVFrame frame, int width, int height, ByteBuffer out) {
        BytePointer yPtr = frame.data(0);
        BytePointer uvPtr = frame.data(1);
        if (yPtr == null || uvPtr == null) return false;
        int yStride = frame.linesize(0);
        int uvStride = frame.linesize(1);

        // Read Y plane
        for (int r = 0; r < height; r++) {
            yPtr.position(r * yStride);
            yPtr.get(yPlane, r * width, width);
        }
        // Read UV plane
        for (int r = 0; r < height / 2; r++) {
            uvPtr.position(r * uvStride);
            uvPtr.get(uvPlane, r * width, width);
        }

        out.rewind();
        for (int row = 0; row < height; row++) {
            int uvRow = (row / 2) * width;
            for (int col = 0; col < width; col++) {
                int Y = yPlane[row * width + col] & 0xFF;
                int uvIndex = uvRow + (col & ~1);
                int U = uvPlane[uvIndex] & 0xFF;
                int V = uvPlane[uvIndex + 1] & 0xFF;

                int c = Y - 16;
                int d = U - 128;
                int e = V - 128;
                int rVal = (298 * c + 409 * e + 128) >> 8;
                int gVal = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int bVal = (298 * c + 516 * d + 128) >> 8;
                if (rVal < 0) rVal = 0; else if (rVal > 255) rVal = 255;
                if (gVal < 0) gVal = 0; else if (gVal > 255) gVal = 255;
                if (bVal < 0) bVal = 0; else if (bVal > 255) bVal = 255;

                out.put((byte) rVal);
                out.put((byte) gVal);
                out.put((byte) bVal);
            }
        }
        out.rewind();
        return true;
    }

    private boolean convertYUV420PToRGB(AVFrame frame, int width, int height, ByteBuffer out) {
        BytePointer yPtr = frame.data(0);
        BytePointer uPtr = frame.data(1);
        BytePointer vPtr = frame.data(2);
        if (yPtr == null || uPtr == null || vPtr == null) return false;
        int yStride = frame.linesize(0);
        int uStride = frame.linesize(1);
        int vStride = frame.linesize(2);

        for (int r = 0; r < height; r++) {
            yPtr.position(r * yStride);
            yPtr.get(yPlane, r * width, width);
        }
        for (int r = 0; r < height / 2; r++) {
            uPtr.position(r * uStride);
            uPtr.get(uPlane, r * (width / 2), width / 2);
            vPtr.position(r * vStride);
            vPtr.get(vPlane, r * (width / 2), width / 2);
        }

        out.rewind();
        for (int row = 0; row < height; row++) {
            int uRow = (row / 2) * (width / 2);
            for (int col = 0; col < width; col++) {
                int Y = yPlane[row * width + col] & 0xFF;
                int U = uPlane[uRow + (col / 2)] & 0xFF;
                int V = vPlane[uRow + (col / 2)] & 0xFF;

                int c = Y - 16;
                int d = U - 128;
                int e = V - 128;
                int rVal = (298 * c + 409 * e + 128) >> 8;
                int gVal = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int bVal = (298 * c + 516 * d + 128) >> 8;
                if (rVal < 0) rVal = 0; else if (rVal > 255) rVal = 255;
                if (gVal < 0) gVal = 0; else if (gVal > 255) gVal = 255;
                if (bVal < 0) bVal = 0; else if (bVal > 255) bVal = 255;

                out.put((byte) rVal);
                out.put((byte) gVal);
                out.put((byte) bVal);
            }
        }
        out.rewind();
        return true;
    }
}
