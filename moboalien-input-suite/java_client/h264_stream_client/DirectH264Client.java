package java_client.h264_stream_client;

import java_client.screenshare_client_lib.FrameAssembly;
import java_client.screenshare_client_lib.H264StreamReceiver;
import org.bytedeco.ffmpeg.global.avcodec;
import org.bytedeco.ffmpeg.global.avutil;
import org.bytedeco.ffmpeg.global.swscale;
import org.bytedeco.ffmpeg.avcodec.*;
import org.bytedeco.ffmpeg.avutil.*;
import org.bytedeco.ffmpeg.swscale.*;
import org.bytedeco.javacpp.BytePointer;
import org.bytedeco.javacpp.IntPointer;
import org.bytedeco.javacpp.Pointer;

import javax.swing.*;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferByte;

public class DirectH264Client extends JFrame {
    private final VideoRenderer renderer;
    private H264StreamReceiver streamReceiver;
    private AVCodecContext codecContext;
    private AVCodec codec;
    private AVFrame frame;
    private AVPacket packet;
    private BytePointer encodedBuffer;
    private volatile boolean running = true;
    private int frameCount = 0;
    private long lastFpsTime = System.currentTimeMillis();
    private int fpsCounter = 0;

    public DirectH264Client() {
        setTitle("Direct H.264 Decoder");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setSize(800, 600);
        setLocationRelativeTo(null);

        // Allow choosing renderer via system property or later command-line arg. Default to Swing renderer.
        String use = System.getProperty("video.renderer");
        if (use == null) use = "swing";
        if (use.equalsIgnoreCase("javafx")) {
            renderer = new JavaFXYuvRenderer();
        } else if (use.equalsIgnoreCase("gl") || use.equalsIgnoreCase("gpu")) {
            renderer = new GLYuvRenderer();
        } else {
            renderer = new SwingVideoRenderer();
        }
        add((Component) renderer.getComponent(), BorderLayout.CENTER);

        addWindowListener(new java.awt.event.WindowAdapter() {
            @Override
            public void windowClosing(java.awt.event.WindowEvent windowEvent) {
                stop();
            }
        });

        initializeDecoder();
        
        // Repaint at ~30 FPS to save CPU, decoupled from decoder speed
        new Timer(33, e -> {
            Component comp = renderer.getComponent();
            if (comp != null) comp.repaint();
        }).start();
    }

    private void initializeDecoder() {
        try {
            // Try hardware decoder first
            codec = avcodec.avcodec_find_decoder_by_name("h264_qsv");
            if (codec == null) {
                codec = avcodec.avcodec_find_decoder_by_name("h264_cuvid");
            }
            if (codec == null) {
                codec = avcodec.avcodec_find_decoder(avcodec.AV_CODEC_ID_H264);
                System.out.println("Using software H.264 decoder");
            } else {
                System.out.println("Using hardware H.264 decoder: " + codec.name().getString());
            }
            
            if (codec == null) {
                throw new RuntimeException("H.264 decoder not found");
            }

            codecContext = avcodec.avcodec_alloc_context3(codec);
            if (codecContext == null) {
                throw new RuntimeException("Could not allocate codec context");
            }
            
            codecContext.thread_count(0);

            if (avcodec.avcodec_open2(codecContext, codec, (AVDictionary) null) < 0) {
                throw new RuntimeException("Could not open codec");
            }

            frame = avutil.av_frame_alloc();
            packet = avcodec.av_packet_alloc();

            System.out.println("H.264 decoder initialized successfully");
        } catch (Exception e) {
            e.printStackTrace();
            throw new RuntimeException("Failed to initialize decoder", e);
        }
    }

    public void connect(String host, int port) {
        System.out.println("Connecting to " + host + ":" + port);
        
        streamReceiver = new H264StreamReceiver();
        streamReceiver.connect(host, port);
        
        new Thread(() -> {
            try {
                while (running) {
                    FrameAssembly fa = streamReceiver.takeFrame();
                    byte[] frameData = fa.getAssembledFrame();
                    if (frameData != null) {
                        decodeFrame(frameData, fa.isConfig(), fa.isKeyFrame());
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }).start();
    }

    private void decodeFrame(byte[] frameData, boolean isConfig, boolean isKeyFrame) {
        try {
            if (encodedBuffer == null || encodedBuffer.capacity() < frameData.length) {
                if (encodedBuffer != null) encodedBuffer.close();
                encodedBuffer = new BytePointer(frameData.length);
            }
            encodedBuffer.position(0).put(frameData);
            
            packet.data(encodedBuffer);
            packet.size(frameData.length);

            int ret = avcodec.avcodec_send_packet(codecContext, packet);
            if (ret < 0) return;

            ret = avcodec.avcodec_receive_frame(codecContext, frame);
            if (ret == avutil.AVERROR_EAGAIN() || ret == avutil.AVERROR_EOF()) {
                return;
            } else if (ret < 0) {
                return;
            }

            // Delegate rendering to the pluggable renderer (may drop unsupported formats)
            renderer.renderFrame(frame);
            frameCount++;
            fpsCounter++;
            
            long currentTime = System.currentTimeMillis();
            if (currentTime - lastFpsTime >= 1000) {
                double fps = fpsCounter * 1000.0 / (currentTime - lastFpsTime);
                final String title = String.format("Direct H.264 Decoder - Frame %d (%.1f FPS)", frameCount, fps);
                lastFpsTime = currentTime;
                fpsCounter = 0;
                SwingUtilities.invokeLater(() -> setTitle(title));
            }
        } catch (Exception e) {
            // Ignore errors for performance
        }
    }

    // Renderer implementations handle frame conversion & UI. See `SwingVideoRenderer` for the default implementation.

    public void stop() {
        running = false;
        if (streamReceiver != null) {
            streamReceiver.stop();
        }
        
        // Cleanup FFmpeg resources
        if (encodedBuffer != null) {
            encodedBuffer.close();
        }
        if (frame != null) {
            avutil.av_frame_free(frame);
        }
        if (packet != null) {
            avcodec.av_packet_free(packet);
        }
        if (codecContext != null) {
            avcodec.avcodec_free_context(codecContext);
        }
        if (renderer != null) {
            renderer.stop();
        }
    }

    public static void main(String[] args) {
        if (args.length < 2) {
            System.out.println("Usage: java DirectH264Client <host> <port>");
            return;
        }
        String host = args[0];
        int port = Integer.parseInt(args[1]);
        if (args.length >= 3) {
            if (args[2].equalsIgnoreCase("javafx")) {
                System.setProperty("video.renderer", "javafx");
            } else if (args[2].equalsIgnoreCase("gl") || args[2].equalsIgnoreCase("gpu")) {
                System.setProperty("video.renderer", "gl");
            }
        }

        SwingUtilities.invokeLater(() -> {
            DirectH264Client client = new DirectH264Client();
            client.setVisible(true);
            client.connect(host, port);
        });
    }


}