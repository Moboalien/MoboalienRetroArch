package java_client.controller_lib;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;

/**
 * A Java implementation of the controller network protocol for sending input data
 * to the C++ controller_server.
 *
 * This class provides static methods to create byte array packets that match the
 * format expected by the server. It handles the correct byte order (big-endian)
 * for all multi-byte data types.
 */
public class ControllerProtocol {

    // Packet type constants, mirroring the C++ enum PacketType
    public static final byte PACKET_CONFIG = 0;
    public static final byte PACKET_STATE = 1;
    public static final byte PACKET_MOUSE = 2;
    public static final byte PACKET_XINPUT = 3;
    public static final byte PACKET_MOUSE_CONFIG = 4;
    public static final byte PACKET_SCREEN_CONFIG = 5;
    public static final byte PACKET_SCREEN_STOP = 6;
    public static final byte PACKET_TEXT = 7;
    public static final byte PACKET_COMMAND = 8;
    public static final byte PACKET_KEYCODE_DOWN = 9;
    public static final byte PACKET_KEYCODE_UP = 10;
    public static final byte PACKET_HELLO = 11;
    public static final byte PACKET_HELLO_RESPONSE = 12;
    public static final byte PACKET_AUTH = 13;
    public static final byte PACKET_AUTH_RESULT = 14;
    public static final byte PACKET_SET_MOUSE_MODE = 15;
    public static final byte PACKET_REQUEST_KEYFRAME = 16;

    // Mouse mode constants
    public static final byte MOUSE_MODE_RELATIVE = 0;
    public static final byte MOUSE_MODE_ABSOLUTE = 1;
    public static final byte MOUSE_MODE_TOUCHSCREEN = 2;

    // Special virtual key codes for mouse functionality
    // Using range 0x1000-0x1FFF to avoid conflicts with Windows VK codes (0x01-0xFF)
    public static final int VK_MOUSE_LEFT_BUTTON = 0x1001;
    public static final int VK_MOUSE_RIGHT_BUTTON = 0x1002;
    public static final int VK_MOUSE_MIDDLE_BUTTON = 0x1003;
    public static final int VK_MOUSE_X1_BUTTON = 0x1004;
    public static final int VK_MOUSE_X2_BUTTON = 0x1005;
    public static final int VK_MOUSE_MOVE_X = 0x1010;
    public static final int VK_MOUSE_MOVE_Y = 0x1011;
    public static final int VK_MOUSE_WHEEL = 0x1020;

    // Special virtual key codes for analog joystick axes
    public static final int VK_JOYSTICK_LX = 0x1030;
    public static final int VK_JOYSTICK_LY = 0x1031;
    public static final int VK_JOYSTICK_RX = 0x1032;
    public static final int VK_JOYSTICK_RY = 0x1033;

    /**
     * Creates a configuration packet to define the virtual key codes for the buttons.
     *
     * @param buttonCodes A list of integer virtual key codes.
     * @return A byte array representing the config packet.
     */
    public static byte[] createConfigPacket(List<Integer> buttonCodes) {
        // Convert List<Integer> to int[] and call the array-based overload.
        int[] codes = new int[buttonCodes.size()];
        for (int i = 0; i < buttonCodes.size(); i++) {
            codes[i] = buttonCodes.get(i);
        }
        return createConfigPacket(codes);
    }

    /**
     * Creates a configuration packet to define the virtual key codes for the buttons.
     * (Overload for primitive array)
     *
     * @param buttonCodes An array of integer virtual key codes.
     * @return A byte array representing the config packet.
     */
    public static byte[] createConfigPacket(int[] buttonCodes) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_CONFIG);
            dos.writeInt(buttonCodes.length); // Number of buttons
            for (int code : buttonCodes) {
                dos.writeInt(code); // Each button's key code
            }
            return baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create config packet", e);
        }
    }

    /**
     * Creates a state packet containing the analog duty cycles for the configured buttons.
     *
     * @param states A list of float values (0.0 to 1.0) corresponding to each button in the config.
     * @return A byte array representing the state packet.
     */
    public static byte[] createStatePacket(List<Float> states) {
        // Convert List<Float> to float[] and call the array-based overload.
        float[] stateArray = new float[states.size()];
        for (int i = 0; i < states.size(); i++) {
            stateArray[i] = states.get(i);
        }
        return createStatePacket(stateArray);
    }

    /**
     * Creates a state packet containing the analog duty cycles for the configured buttons.
     * (Overload for primitive array)
     *
     * @param states An array of float values (0.0 to 1.0) corresponding to each button in the config.
     * @return A byte array representing the state packet.
     */
    public static byte[] createStatePacket(float[] states) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_STATE);
            for (float state : states) {
                dos.writeFloat(state);
            }
            return baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create state packet", e);
        }
    }

    /**
     * Creates a mouse packet for sending mouse movement, wheel, and button states.
     *
     * @param x       The absolute or relative X coordinate.
     * @param y       The absolute or relative Y coordinate.
     * @param wheel   The wheel movement value.
     * @param buttons A bitmask of the mouse button states.
     * @return A byte array representing the mouse packet.
     */
    public static byte[] createMousePacket(float x, float y, float wheel, int buttons) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_MOUSE);
            dos.writeFloat(x);
            dos.writeFloat(y);
            dos.writeFloat(wheel);
            dos.writeInt(buttons);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create mouse packet", e);
        }
    }

    /**
     * Creates a mouse configuration packet to set the mouse mode.
     *
     * @param mode The mouse mode: 0=relative, 1=absolute, 2=touchscreen.
     * @return A byte array representing the mouse config packet.
     */
    public static byte[] createMouseConfigPacket(byte mode) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_MOUSE_CONFIG);
            dos.writeByte(mode);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create mouse config packet", e);
        }
    }

    /**
     * Creates a packet to set the mouse mode.
     *
     * @param mode The mouse mode: 0=relative, 1=absolute, 2=touchscreen.
     * @return A byte array representing the set mouse mode packet.
     */
    public static byte[] createSetMouseModePacket(byte mode) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {
            dos.writeByte(PACKET_SET_MOUSE_MODE);
            dos.writeByte(mode);
            return baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create set mouse mode packet", e);
        }
    }

    /**
     * Creates a packet to request a keyframe (full refresh) from the screen server.
     *
     * @return A byte array representing the request keyframe packet.
     */
    public static byte[] createRequestKeyframePacket() {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {
            dos.writeByte(PACKET_REQUEST_KEYFRAME);
            return baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create request keyframe packet", e);
        }
    }

    /**
     * Creates an XInput packet for emulating an Xbox controller.
     *
     * @param controllerIndex The index of the controller (0-3).
     * @param buttons         A bitmask of the XInput button states.
     * @param lt              Left trigger value (0.0 to 1.0).
     * @param rt              Right trigger value (0.0 to 1.0).
     * @param lx              Left thumbstick X-axis (-1.0 to 1.0).
     * @param ly              Left thumbstick Y-axis (-1.0 to 1.0).
     * @param rx              Right thumbstick X-axis (-1.0 to 1.0).
     * @param ry              Right thumbstick Y-axis (-1.0 to 1.0).
     * @return A byte array representing the XInput packet.
     */
    public static byte[] createXInputPacket(int controllerIndex, short buttons, float lt, float rt, float lx, float ly, float rx, float ry) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos)) {
            dos.writeByte(PACKET_XINPUT);
            dos.writeByte(controllerIndex); // uint8_t
            dos.writeShort(buttons);        // uint16_t
            dos.writeFloat(lt);
            dos.writeFloat(rt);
            dos.writeFloat(lx);
            dos.writeFloat(ly);
            dos.writeFloat(rx);
            dos.writeFloat(ry);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create XInput packet", e);
        }
    }

    /**
     * Represents the configuration for screen streaming.
     * Mirrors the C++ ScreenConfigPacket struct.
     */
    public static class ScreenConfig {
        // Method constants
        public static final byte METHOD_GDI = 0;
        public static final byte METHOD_DD = 1;
        
        // Streaming mode constants
        public static final byte STREAMING_MJPEG = 0;
        public static final byte STREAMING_DIFFERENTIAL = 1;
        public static final byte STREAMING_H264 = 2;
        
        public byte fps;
        public int bitrate; // in kbps
        public float scale;
        public byte quality;
        public byte method; // 0=GDI, 1=DD
        public byte streamingMode; // 0=MJPEG, 1=Differential
        public byte minQuality;
        public int clientBuffer; // in KB
        public boolean cursorEnabled;

        public ScreenConfig(byte fps, int bitrate, float scale, byte quality, byte method, byte streamingMode, byte minQuality, int clientBuffer, boolean cursorEnabled) {
            this.fps = fps;
            this.bitrate = bitrate;
            this.scale = scale;
            this.quality = quality;
            this.method = method;
            this.streamingMode = streamingMode;
            this.minQuality = minQuality;
            this.clientBuffer = clientBuffer;
            this.cursorEnabled = cursorEnabled;
        }
    }

    /**
     * Creates a screen configuration packet to trigger the screen server.
     *
     * @param config The ScreenConfig object containing all streaming parameters.
     * @return A byte array representing the screen config packet.
     */
    public static byte[] createScreenConfigPacket(ScreenConfig config) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_SCREEN_CONFIG);
            dos.writeByte(config.fps);
            dos.writeInt(config.bitrate);
            dos.writeFloat(config.scale);
            dos.writeByte(config.quality);
            dos.writeByte(config.method);
            dos.writeByte(config.streamingMode);
            dos.writeByte(config.minQuality);
            dos.writeInt(config.clientBuffer);
            dos.writeByte(config.cursorEnabled ? 1 : 0);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create screen config packet", e);
        }
    }

    /**
     * Creates a screen stop packet to terminate the screen server.
     *
     * @return A byte array representing the screen stop packet.
     */
    public static byte[] createScreenStopPacket() {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_SCREEN_STOP);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create screen stop packet", e);
        }
    }

    /**
     * Creates a text input packet for sending text to be typed.
     *
     * @param text The text string to be typed.
     * @return A byte array representing the text input packet.
     */
    public static byte[] createTextInputPacket(String text) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            byte[] textBytes = text.getBytes("UTF-8");
            dos.writeByte(PACKET_TEXT);
            dos.writeShort(textBytes.length);
            dos.write(textBytes);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create text input packet", e);
        }
    }

    /**
     * Creates a command execution packet for running commands in cmd prompt.
     *
     * @param command The command string to be executed.
     * @return A byte array representing the command packet.
     */
    public static byte[] createCommandPacket(String command) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            byte[] commandBytes = command.getBytes("UTF-8");
            dos.writeByte(PACKET_COMMAND);
            dos.writeShort(commandBytes.length);
            dos.write(commandBytes);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create command packet", e);
        }
    }

    /**
     * Creates a keycode down packet for sending virtual key codes to be pressed.
     *
     * @param keycodes Array of virtual key codes to be pressed down.
     * @return A byte array representing the keycode down packet.
     */
    public static byte[] createKeycodeDownPacket(int[] keycodes) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_KEYCODE_DOWN);
            dos.writeShort(keycodes.length);
            for (int keycode : keycodes) {
                dos.writeInt(keycode);
            }
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create keycode down packet", e);
        }
    }

    /**
     * Creates a keycode up packet for sending virtual key codes to be released.
     *
     * @param keycodes Array of virtual key codes to be released.
     * @return A byte array representing the keycode up packet.
     */
    public static byte[] createKeycodeUpPacket(int[] keycodes) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_KEYCODE_UP);
            dos.writeShort(keycodes.length);
            for (int keycode : keycodes) {
                dos.writeInt(keycode);
            }
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create keycode up packet", e);
        }
    }

    /**
     * Creates a hello packet to initiate handshake.
     *
     * @param version      Client version.
     * @param serviceFlags Bitmask of required services.
     * @return A byte array representing the hello packet.
     */
    public static byte[] createHelloPacket(int version, int serviceFlags) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {

            dos.writeByte(PACKET_HELLO);
            dos.writeInt(version);
            dos.writeInt(serviceFlags);
            return baos.toByteArray();

        } catch (IOException e) {
            throw new RuntimeException("Failed to create hello packet", e);
        }
    }

    /**
     * Represents a parsed HelloResponse packet.
     */
    public static class HelloResponse {
        public int capabilities;
        public int version;
        public int controllerPort;
        public int screenPort;
        public long serverId;
        public int serverType;
        public boolean passwordRequired;
        public byte[] salt;

        public HelloResponse(int capabilities, int version, int controllerPort, int screenPort, long serverId, int serverType, boolean passwordRequired, byte[] salt) {
            this.capabilities = capabilities;
            this.version = version;
            this.controllerPort = controllerPort;
            this.screenPort = screenPort;
            this.serverId = serverId;
            this.serverType = serverType;
            this.passwordRequired = passwordRequired;
            this.salt = salt;
        }
    }

    /**
     * Parse a HelloResponse packet (expects the full packet, including the leading type byte).
     * Throws IllegalArgumentException on malformed input.
     */
    public static HelloResponse parseHelloResponse(byte[] data) {
        if (data == null || data.length < 1) throw new IllegalArgumentException("Empty packet");
        ByteBuffer bb = ByteBuffer.wrap(data);
        bb.order(ByteOrder.BIG_ENDIAN);
        byte type = bb.get();
        if (type != PACKET_HELLO_RESPONSE) throw new IllegalArgumentException("Invalid packet type: " + type);
        if (bb.remaining() < 4 + 4 + 2 + 2 + 8 + 4 + 1 + 1) throw new IllegalArgumentException("HelloResponse too short");
        int capabilities = bb.getInt();
        int version = bb.getInt();
        int controllerPort = bb.getShort() & 0xFFFF;
        int screenPort = bb.getShort() & 0xFFFF;
        long serverId = bb.getLong();
        int serverType = bb.getShort() & 0xFFFF;
        boolean passwordRequired = (bb.get() != 0);
        int saltLen = bb.get() & 0xFF;
        if (bb.remaining() < saltLen) throw new IllegalArgumentException("HelloResponse salt truncated");
        byte[] salt = new byte[saltLen];
        bb.get(salt);
        return new HelloResponse(capabilities, version, controllerPort, screenPort, serverId, serverType, passwordRequired, salt);
    }

    /**
     * Creates an auth packet containing the encrypted challenge.
     *
     * @param encryptedData The encrypted challenge data.
     * @return A byte array representing the auth packet.
     */
    public static byte[] createAuthPacket(byte[] encryptedData) {
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {
            dos.writeByte(PACKET_AUTH);
            dos.writeShort(encryptedData.length);
            dos.write(encryptedData);
            return baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create auth packet", e);
        }
    }
}
