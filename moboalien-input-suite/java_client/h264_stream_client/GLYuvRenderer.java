package java_client.h264_stream_client;

import org.bytedeco.ffmpeg.avutil.AVFrame;
import org.bytedeco.javacpp.BytePointer;

import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;

import javax.swing.*;
import java.awt.*;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_NV12;
import static org.bytedeco.ffmpeg.global.avutil.AV_PIX_FMT_YUV420P;

public class GLYuvRenderer implements VideoRenderer {
    private final JPanel placeholder = new JPanel(new BorderLayout());
    private int width = 0;
    private int height = 0;

    // GLFW window handle
    private long window = 0;
    private Thread renderThread;
    private volatile boolean running = false;

    // GL resources
    private int texY = 0;
    private int texUV = 0;
    private int program = 0;
    private int vao = 0;

    // Buffers for tightly packed upload (owned by render thread)
    private ByteBuffer yBuf;
    private ByteBuffer uvBuf;

    // Pending buffers produced by decoder thread; swapped in on GL thread
    private volatile ByteBuffer pendingY = null;
    private volatile ByteBuffer pendingUV = null;

    // Schedule reinitialization on GL thread when size changes
    private volatile boolean pendingReinit = false;
    private volatile int pendingWidth = 0;
    private volatile int pendingHeight = 0;

    private volatile boolean frameReady = false;

    public GLYuvRenderer() {
        placeholder.add(new JLabel("GPU Window (GLFW) will open when renderer is initialized"), BorderLayout.CENTER);
    }

    @Override
    public Component getComponent() {
        return placeholder;
    }

    @Override
    public void init(int width, int height) {
        if (width <= 0 || height <= 0) return;
        // schedule reinitialization on GL thread to avoid concurrent memFree/memAlloc with uploads
        pendingWidth = width;
        pendingHeight = height;
        pendingReinit = true;

        // Start GLFW window and render thread if not running
        if (window == 0) {
            startGLWindow(width, height);
        }
    }

    private void startGLWindow(int w, int h) {
        GLFWErrorCallback.createPrint(System.err).set();
        if (!GLFW.glfwInit()) {
            throw new IllegalStateException("Unable to initialize GLFW");
        }
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 3);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 3);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_TRUE);

        window = GLFW.glfwCreateWindow(w, h, "Moboalien GPU Renderer", MemoryUtil.NULL, MemoryUtil.NULL);
        if (window == MemoryUtil.NULL) {
            throw new RuntimeException("Failed to create GLFW window");
        }

        renderThread = new Thread(() -> {
            try {
                GLFW.glfwMakeContextCurrent(window);
                GL.createCapabilities();
                initGLResources();

                running = true;
                while (running && !GLFW.glfwWindowShouldClose(window)) {
                    // perform pending reinitialization if requested
                    if (pendingReinit) {
                        // free old buffers and allocate new ones in the GL thread
                        if (yBuf != null) { MemoryUtil.memFree(yBuf); yBuf = null; }
                        if (uvBuf != null) { MemoryUtil.memFree(uvBuf); uvBuf = null; }
                        width = pendingWidth;
                        height = pendingHeight;
                        yBuf = MemoryUtil.memAlloc(width * height);
                        uvBuf = MemoryUtil.memAlloc(width * (height / 2));
                        // Recreate GL resources (textures, program, VAO)
                        initGLResources();
                        pendingReinit = false;
                    }

                    // If a new pending frame was produced by decoder thread, swap it in here
                    if (pendingY != null || pendingUV != null) {
                        // free old buffers owned by GL thread and take ownership of pending ones
                        if (pendingY != null) {
                            if (yBuf != null) MemoryUtil.memFree(yBuf);
                            yBuf = pendingY;
                            pendingY = null;
                        }
                        if (pendingUV != null) {
                            if (uvBuf != null) MemoryUtil.memFree(uvBuf);
                            uvBuf = pendingUV;
                            pendingUV = null;
                        }
                        frameReady = true;
                    }

                    // Render when frame is ready, or keep loop running
                    if (frameReady) {
                        uploadTextures();
                        frameReady = false;
                    }
                    renderGL();
                    GLFW.glfwSwapBuffers(window);
                    GLFW.glfwPollEvents();
                    Thread.sleep(5);
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } finally {
                cleanupGL();
                GLFW.glfwDestroyWindow(window);
                GLFW.glfwTerminate();
                window = 0;
            }
        }, "GL-Renderer-Thread");
        renderThread.setDaemon(true);
        renderThread.start();
    }

    @Override
    public void renderFrame(AVFrame frame) {
        if (frame == null) return;
        int w = frame.width();
        int h = frame.height();
        if (w <= 0 || h <= 0) return;
        if (w != width || h != height) init(w, h);

        int pixFmt = frame.format();
        if (pixFmt == AV_PIX_FMT_NV12) {
            BytePointer yPtr = frame.data(0);
            BytePointer uvPtr = frame.data(1);
            int yStride = frame.linesize(0);
            int uvStride = frame.linesize(1);
            if (yPtr == null || uvPtr == null) return;

            // allocate per-frame direct buffers and fill them, then publish to GL thread
            ByteBuffer newY = MemoryUtil.memAlloc(w * h);
            for (int r = 0; r < h; r++) {
                yPtr.position(r * yStride);
                byte[] row = new byte[w];
                yPtr.get(row, 0, w);
                newY.put(row);
            }
            newY.flip();

            ByteBuffer newUV = MemoryUtil.memAlloc(w * (h / 2));
            for (int r = 0; r < h / 2; r++) {
                uvPtr.position(r * uvStride);
                byte[] row = new byte[w];
                uvPtr.get(row, 0, w);
                newUV.put(row);
            }
            newUV.flip();

            // free any previous pending buffers (to avoid leaks)
            ByteBuffer oldPendingY = pendingY;
            if (oldPendingY != null) { MemoryUtil.memFree(oldPendingY); }
            ByteBuffer oldPendingUV = pendingUV;
            if (oldPendingUV != null) { MemoryUtil.memFree(oldPendingUV); }

            pendingY = newY;
            pendingUV = newUV;
            frameReady = true;
        } else if (pixFmt == AV_PIX_FMT_YUV420P) {
            BytePointer yPtr = frame.data(0);
            BytePointer uPtr = frame.data(1);
            BytePointer vPtr = frame.data(2);
            int yStride = frame.linesize(0);
            int uStride = frame.linesize(1);
            int vStride = frame.linesize(2);
            if (yPtr == null || uPtr == null || vPtr == null) return;

            ByteBuffer newY = MemoryUtil.memAlloc(w * h);
            for (int r = 0; r < h; r++) {
                yPtr.position(r * yStride);
                byte[] row = new byte[w];
                yPtr.get(row, 0, w);
                newY.put(row);
            }
            newY.flip();

            ByteBuffer newUV = MemoryUtil.memAlloc(w * (h / 2));
            for (int r = 0; r < h / 2; r++) {
                uPtr.position(r * uStride);
                vPtr.position(r * vStride);
                for (int c = 0; c < w; c += 2) {
                    int u = uPtr.get(c / 2) & 0xFF;
                    int v = vPtr.get(c / 2) & 0xFF;
                    newUV.put((byte) u);
                    newUV.put((byte) v);
                }
            }
            newUV.flip();

            ByteBuffer oldPendingY2 = pendingY;
            if (oldPendingY2 != null) { MemoryUtil.memFree(oldPendingY2); }
            ByteBuffer oldPendingUV2 = pendingUV;
            if (oldPendingUV2 != null) { MemoryUtil.memFree(oldPendingUV2); }

            pendingY = newY;
            pendingUV = newUV;
            frameReady = true;
        }
    }

    @Override
    public void stop() {
        running = false;
        if (renderThread != null) {
            try { renderThread.join(2000); } catch (InterruptedException ignored) {}
        }
        if (yBuf != null) { MemoryUtil.memFree(yBuf); yBuf = null; }
        if (uvBuf != null) { MemoryUtil.memFree(uvBuf); uvBuf = null; }
        if (pendingY != null) { MemoryUtil.memFree(pendingY); pendingY = null; }
        if (pendingUV != null) { MemoryUtil.memFree(pendingUV); pendingUV = null; }
    }

    private void initGLResources() {
        // delete old
        if (texY != 0) GL11.glDeleteTextures(texY);
        if (texUV != 0) GL11.glDeleteTextures(texUV);
        if (program != 0) GL20.glDeleteProgram(program);
        if (vao != 0) GL30.glDeleteVertexArrays(vao);

        // Create textures
        texY = GL11.glGenTextures();
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, texY);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
        GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL30.GL_R8, width, height, 0, GL11.GL_RED, GL11.GL_UNSIGNED_BYTE, (ByteBuffer) null);

        texUV = GL11.glGenTextures();
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, texUV);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
        // UV is half height, but width in our uvBuf is full width bytes per row (interleaved U,V), so we'll use width/2 x height/2 RG8
        GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL30.GL_RG8, width / 2, height / 2, 0, GL30.GL_RG, GL11.GL_UNSIGNED_BYTE, (ByteBuffer) null);

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);

        program = createShaderProgram();

        // Setup a simple full-screen VAO/VBO
        vao = GL30.glGenVertexArrays();
        int vbo = GL15.glGenBuffers();
        GL30.glBindVertexArray(vao);
        float[] verts = new float[] {
            // x, y, u, v
            -1f, -1f, 0f, 0f,
             1f, -1f, 1f, 0f,
             1f,  1f, 1f, 1f,
            -1f,  1f, 0f, 1f
        };
        java.nio.FloatBuffer fb = MemoryUtil.memAllocFloat(verts.length);
        fb.put(verts).flip();
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, vbo);
        GL15.glBufferData(GL15.GL_ARRAY_BUFFER, fb, GL15.GL_STATIC_DRAW);
        // attribute locations are layout(location = 0) and (location = 1) in the shader
        GL20.glEnableVertexAttribArray(0);
        GL20.glVertexAttribPointer(0, 2, GL11.GL_FLOAT, false, 4 * Float.BYTES, 0);
        GL20.glEnableVertexAttribArray(1);
        GL20.glVertexAttribPointer(1, 2, GL11.GL_FLOAT, false, 4 * Float.BYTES, 2 * Float.BYTES);
        MemoryUtil.memFree(fb);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, 0);
        GL30.glBindVertexArray(0);

        GL20.glUseProgram(program);
        GL20.glUniform1i(GL20.glGetUniformLocation(program, "texY"), 0);
        GL20.glUniform1i(GL20.glGetUniformLocation(program, "texUV"), 1);
        // default: no UV swap
        int locSwap = GL20.glGetUniformLocation(program, "swapUV");
        if (locSwap != -1) GL20.glUniform1i(locSwap, 0);
        GL20.glUseProgram(0);

        // Setup blending / viewport
        GL11.glDisable(GL11.GL_DEPTH_TEST);
    }

    private void uploadTextures() {
        // Validate buffers to avoid passing invalid pointers into native GL
        int expectedY = width * height;
        int expectedUV = width * (height / 2);
        if (yBuf == null || !yBuf.isDirect() || yBuf.remaining() < expectedY) {
            System.err.println("GL upload: invalid yBuf (direct=" + (yBuf != null && yBuf.isDirect()) + ", remaining=" + (yBuf == null ? 0 : yBuf.remaining()) + ", expected=" + expectedY + ") - skipping upload");
            return;
        }
        if (uvBuf == null || !uvBuf.isDirect() || uvBuf.remaining() < expectedUV) {
            System.err.println("GL upload: invalid uvBuf (direct=" + (uvBuf != null && uvBuf.isDirect()) + ", remaining=" + (uvBuf == null ? 0 : uvBuf.remaining()) + ", expected=" + expectedUV + ") - skipping upload");
            return;
        }

        // upload Y
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, texY);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
        yBuf.position(0);
        GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, width, height, GL11.GL_RED, GL11.GL_UNSIGNED_BYTE, yBuf);
        int err = GL11.glGetError();
        if (err != GL11.GL_NO_ERROR) System.err.println("glTexSubImage2D Y error: 0x" + Integer.toHexString(err));

        // upload UV (width/2 x height/2) from packed interleaved UV rows
        GL13.glActiveTexture(GL13.GL_TEXTURE1);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, texUV);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
        uvBuf.position(0);
        GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL30.GL_RG, GL11.GL_UNSIGNED_BYTE, uvBuf);
        err = GL11.glGetError();
        if (err != GL11.GL_NO_ERROR) System.err.println("glTexSubImage2D UV error: 0x" + Integer.toHexString(err));
    }

    private void cleanupGL() {
        if (texY != 0) { GL11.glDeleteTextures(texY); texY = 0; }
        if (texUV != 0) { GL11.glDeleteTextures(texUV); texUV = 0; }
        if (program != 0) { GL20.glDeleteProgram(program); program = 0; }
        if (vao != 0) { GL30.glDeleteVertexArrays(vao); vao = 0; }
    }

    private void renderGL() {
        if (width == 0 || height == 0) return;
        GL11.glViewport(0, 0, width, height);
        GL11.glClearColor(0f, 0f, 0f, 1f);
        GL11.glClear(GL11.GL_COLOR_BUFFER_BIT);

        if (frameReady) {
            // upload textures
            GL13.glActiveTexture(GL13.GL_TEXTURE0);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, texY);
            // Unpack alignment should be 1
            GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
            GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, width, height, GL11.GL_RED, GL11.GL_UNSIGNED_BYTE, yBuf);

            GL13.glActiveTexture(GL13.GL_TEXTURE1);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, texUV);
            // UV texture stored as width/2 x height/2 RG
            GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
            // Our uvBuf is packed as interleaved U,V per pixel in rows of 'width' bytes, but texture expects width/2 columns with two channels per texel
            // So we can pass uvBuf but set the width/2 and height/2
            GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL30.GL_RG, GL11.GL_UNSIGNED_BYTE, uvBuf);
        }

        // Render
        GL20.glUseProgram(program);
        GL30.glBindVertexArray(vao);
        GL11.glDrawArrays(GL11.GL_TRIANGLE_FAN, 0, 4);
        GL30.glBindVertexArray(0);
        GL20.glUseProgram(0);

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);
    }
    private int createShaderProgram() {
        String vs = "#version 330 core\n" +
            "layout(location = 0) in vec2 aPos;\n" +
            "layout(location = 1) in vec2 aTex;\n" +
            "out vec2 vTex;\n" +
            "void main() { vTex = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
        String fs = "#version 330 core\n" +
            "in vec2 vTex;\n" +
            "out vec4 FragColor;\n" +
            "uniform sampler2D texY;\n" +
            "uniform sampler2D texUV;\n" +
            "uniform int swapUV; // 0 = U in .r, V in .g ; 1 = swap them\n" +
            "void main() {\n" +
            "  float y = texture(texY, vTex).r;\n" +
            "  vec2 uv = texture(texUV, vTex).rg;\n" +
            "  float u = uv.r;\n            " +
            "  float v = uv.g;\n" +
            "  if (swapUV == 1) { float tmp = u; u = v; v = tmp; }\n" +
            "  // BT.601 limited range (video): Y range [16/255,235/255], U/V centered at 128/255\n" +
            "  float Y = (y - 16.0/255.0) / (219.0/255.0);\n" +
            "  float U = u - 0.5;\n" +
            "  float V = v - 0.5;\n" +
            "  float r = 1.164383 * Y + 1.596027 * V;\n" +
            "  float g = 1.164383 * Y - 0.391762 * U - 0.812968 * V;\n" +
            "  float b = 1.164383 * Y + 2.017232 * U;\n" +
            "  FragColor = vec4(r, g, b, 1.0);\n" +
            "}\n";

        int vsId = GL20.glCreateShader(GL20.GL_VERTEX_SHADER);
        GL20.glShaderSource(vsId, vs);
        GL20.glCompileShader(vsId);
        int[] status = new int[1];
        GL20.glGetShaderiv(vsId, GL20.GL_COMPILE_STATUS, status);
        if (status[0] == GL11.GL_FALSE) {
            String log = GL20.glGetShaderInfoLog(vsId);
            System.err.println("Vertex shader compile error: " + log);
        }

        int fsId = GL20.glCreateShader(GL20.GL_FRAGMENT_SHADER);
        GL20.glShaderSource(fsId, fs);
        GL20.glCompileShader(fsId);
        GL20.glGetShaderiv(fsId, GL20.GL_COMPILE_STATUS, status);
        if (status[0] == GL11.GL_FALSE) {
            String log = GL20.glGetShaderInfoLog(fsId);
            System.err.println("Fragment shader compile error: " + log);
        }

        int prog = GL20.glCreateProgram();
        GL20.glAttachShader(prog, vsId);
        GL20.glAttachShader(prog, fsId);
        GL20.glBindAttribLocation(prog, 0, "aPos");
        GL20.glBindAttribLocation(prog, 1, "aTex");
        GL20.glLinkProgram(prog);
        GL20.glGetProgramiv(prog, GL20.GL_LINK_STATUS, status);
        if (status[0] == GL11.GL_FALSE) {
            String log = GL20.glGetProgramInfoLog(prog);
            System.err.println("Program link error: " + log);
        }

        GL20.glDeleteShader(vsId);
        GL20.glDeleteShader(fsId);
        return prog;
    }
}
