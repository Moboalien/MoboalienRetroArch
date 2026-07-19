package java_client.h264_stream_client;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaFormat;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.IOException;
import java.nio.ByteBuffer;

public class H264StreamClient extends SurfaceView implements SurfaceHolder.Callback {
    private MediaCodec decoder;
    private Surface surface;
    private H264StreamReceiver streamReceiver;
    private volatile boolean running = false;
    private Thread decoderThread;

    public H264StreamClient(Context context) {
        super(context);
        getHolder().addCallback(this);
    }

    public void connect(String host, int port) {
        streamReceiver = new H264StreamReceiver();
        streamReceiver.connect(host, port);
        startDecoding();
    }

    private void startDecoding() {
        running = true;
        decoderThread = new Thread(() -> {
            try {
                while (running) {
                    FrameAssembly fa = streamReceiver.takeFrame();
                    byte[] frameData = fa.getAssembledFrame();
                    if (frameData != null && decoder != null) {
                        decodeFrame(frameData, fa.isKeyFrame());
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        decoderThread.start();
    }

    private void decodeFrame(byte[] frameData, boolean isKeyFrame) {
        try {
            int inputIndex = decoder.dequeueInputBuffer(10000);
            if (inputIndex >= 0) {
                ByteBuffer inputBuffer = decoder.getInputBuffer(inputIndex);
                inputBuffer.clear();
                inputBuffer.put(frameData);
                int flags = isKeyFrame ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
                decoder.queueInputBuffer(inputIndex, 0, frameData.length, System.nanoTime() / 1000, flags);
            }

            MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
            int outputIndex = decoder.dequeueOutputBuffer(bufferInfo, 0);
            if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                MediaFormat newFormat = decoder.getOutputFormat();
                int width = newFormat.getInteger(MediaFormat.KEY_WIDTH);
                int height = newFormat.getInteger(MediaFormat.KEY_HEIGHT);
                System.out.println("Resolution changed: " + width + "x" + height);
                // Decoder handles format change automatically
            } else if (outputIndex >= 0) {
                decoder.releaseOutputBuffer(outputIndex, true);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void stop() {
        running = false;
        if (decoderThread != null) {
            decoderThread.interrupt();
        }
        if (streamReceiver != null) {
            streamReceiver.stop();
        }
        releaseDecoder();
    }

    private void initDecoder() {
        try {
            decoder = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 1920, 1080);
            decoder.configure(format, surface, null, 0);
            decoder.start();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void releaseDecoder() {
        if (decoder != null) {
            try {
                decoder.stop();
                decoder.release();
            } catch (Exception e) {
                e.printStackTrace();
            }
            decoder = null;
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surface = holder.getSurface();
        initDecoder();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stop();
    }
}
