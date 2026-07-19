package java_client.differential_screen_client;

import javax.imageio.ImageIO;
import javax.swing.*;

import java_client.controller_lib.ControllerProtocol;
import java_client.controller_lib.HandshakeClient;
import java_client.controller_lib.PacketEncryptor;

import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

/**
 * A Java client for testing the C++ DifferentialStreamer.
 *
 * This client connects to the server, receives binary packets for screen and
 * cursor updates, and renders them in a window.
 * <p>
 * How to Run:
 * 1. Compile: javac DifferentialClient.java
 * 2. Run:     java DifferentialClient <controller_host> <controller_port>
 *    Example: java DifferentialClient localhost 16234
 * <p>
 * This client will first send a UDP packet to the controller_server to
 * start the screen_server process, then it will connect to the screen_server's
 * streaming port.
 */
public class DifferentialClient extends JFrame {

    private final DrawingPanel drawingPanel;

    public DifferentialClient() {
        setTitle("Differential Stream Viewer");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setSize(800, 600);
        setLocationRelativeTo(null);

        drawingPanel = new DrawingPanel();
        add(drawingPanel, BorderLayout.CENTER);
    }

    public void connect(String host, int port) {
        // Run network operations on a separate thread to avoid freezing the UI
        new Thread(() -> {
            try (Socket socket = new Socket(host, port);
                 DataInputStream dis = new DataInputStream(socket.getInputStream())) {

                System.out.println("Connected to server at " + host + ":" + port);
                setTitle("Differential Stream Viewer - Connected to " + host + ":" + port);

                while (!socket.isClosed()) {
                    readPacket(dis);
                }

            } catch (EOFException e) {
                System.out.println("Server closed the connection.");
                SwingUtilities.invokeLater(() -> setTitle("Differential Stream Viewer - Disconnected"));
            } catch (IOException e) {
                System.err.println("Connection error: " + e.getMessage());
                SwingUtilities.invokeLater(() -> {
                    JOptionPane.showMessageDialog(this,
                            "Connection lost: " + e.getMessage(),
                            "Connection Error",
                            JOptionPane.ERROR_MESSAGE);
                    setTitle("Differential Stream Viewer - Disconnected");
                });
            }
        }).start();
    }

    private void readPacket(DataInputStream dis) throws IOException {
        byte packetType = dis.readByte();

        switch (packetType) {
            case 1: // Frame Packet
                readFramePacket(dis);
                break;
            case 3: // Cursor Packet
                readCursorPacket(dis);
                break;
            default:
                System.err.println("Unknown packet type received: " + packetType);
                // To prevent de-sync, you might want to close the connection here.
                throw new IOException("Unknown packet type: " + packetType);
        }
        // Trigger a repaint after processing a packet
        drawingPanel.repaint();
    }

    private void readFramePacket(DataInputStream dis) throws IOException {
        byte frameType = dis.readByte(); // 1 for diff, 2 for full
        int x = dis.readInt();
        int y = dis.readInt();
        int width = dis.readInt();
        int height = dis.readInt();
        int dataSize = dis.readInt();

        if (dataSize > 0) {
            byte[] jpegData = new byte[dataSize];
            dis.readFully(jpegData);
            drawingPanel.updateScreen(jpegData, x, y, width, height, frameType);
        }
    }

    private void readCursorPacket(DataInputStream dis) throws IOException {
        boolean isMonochrome = dis.readByte() == 1; // Not used in this client, but must be read to stay in sync.
        boolean isVisible = dis.readByte() == 1;
        int x = dis.readInt();
        int y = dis.readInt();
        int dataSize = dis.readInt();


        byte[] pngData = null; 
        if (dataSize > 0) {
            pngData = new byte[dataSize];
            dis.readFully(pngData);
        }
        drawingPanel.updateCursor(pngData, x, y, isVisible, isMonochrome);
    }

    /**
     * Sends a UDP packet to the controller_server to start the screen_server process.
     *
     * @param controllerHost The hostname or IP of the controller server.
     * @param controllerPort The UDP port of the controller server.
     * @param config         The screen configuration to use.
     * @param encryptor      The encryptor from the handshake (optional).
     * @throws IOException If there is a network error.
     */
    private static void triggerScreenServer(String controllerHost, int controllerPort, ControllerProtocol.ScreenConfig config, PacketEncryptor encryptor) throws Exception {
        System.out.println("Sending screen config to controller at " + controllerHost + ":" + controllerPort);

        try (DatagramSocket socket = new DatagramSocket()) {
            byte[] packetData = ControllerProtocol.createScreenConfigPacket(config);
            
            if (encryptor != null) {
                packetData = encryptor.encrypt(packetData);
            }
            System.out.println("Packet size: " + packetData.length + " bytes");
            InetAddress serverAddress = InetAddress.getByName(controllerHost);
            DatagramPacket udpPacket = new DatagramPacket(packetData, packetData.length, serverAddress, controllerPort);
            socket.send(udpPacket);
        }
    }

    public static void main(String[] args) {

        if (args.length < 2) {
            System.out.println("Usage: java DifferentialClient <controller_host> <controller_port>");
            System.out.println("Example: java DifferentialClient 127.0.0.1 16235");
            return;
        }

        String controllerHost = args[0];
        int controllerPort = Integer.parseInt(args[1]);
        String password = "12345678"; // Default password, could be arg[2]
        if (args.length > 2) {
            password = args[2];
        }
        System.out.println("Using password: " + password);

        // Perform Handshake
        System.out.println("Performing handshake with " + controllerHost + ":" + controllerPort);
        HandshakeClient handshake = new HandshakeClient();
        HandshakeClient.HandshakeResult result = handshake.performHandshake(controllerHost, controllerPort, password);

        if (!result.success) {
            System.err.println("Handshake failed: " + result.message);
            return;
        }
        System.out.println("Handshake successful! Controller Port: " + result.controllerPort + ", Screen Port: " + result.screenPort);

        // Define the configuration for the screen server we want to launch.
        // This must match the streaming mode this client is designed for (Differential).
        ControllerProtocol.ScreenConfig config = new ControllerProtocol.ScreenConfig(
                (byte) 30,      // fps
                2000,           // bitrate (kbps)
                1.0f,           // scale
                (byte) 75,      // quality
                (byte) 1,       // method (1=DD)
                (byte) 1,       // streamingMode (1=Differential)
                (byte) 30,      // minQuality
                5120,            // clientBuffer (KB)
                true
        );

        try {
            triggerScreenServer(controllerHost, result.controllerPort, config, result.encryptor);
            // Give the server a moment to start up before we try to connect.
            Thread.sleep(1000);
        } catch (Exception e) {
            System.err.println("Failed to trigger screen server: " + e.getMessage());
            e.printStackTrace();
            return;
        }

        SwingUtilities.invokeLater(() -> {
            DifferentialClient client = new DifferentialClient();
            client.setVisible(true);
            client.connect(controllerHost, result.screenPort);
        });
    }

    /**
     * The JPanel that handles all the rendering.
     */
    private static class DrawingPanel extends JPanel {
        private BufferedImage screenBuffer;
        private BufferedImage cursorImage;
        private int cursorX, cursorY;
        private boolean isCursorVisible = false;
        private boolean isCursorMonochrome = false;

        public void updateScreen(byte[] jpegData, int x, int y, int width, int height, int frameType) {
            try {
                BufferedImage diffImage = ImageIO.read(new ByteArrayInputStream(jpegData));
                if (diffImage == null) return;

                // If this is the first frame, or a full frame, (re)create the buffer
                // A differential frame (type 1) cannot be processed if we don't have a buffer yet.
                if (screenBuffer == null && frameType == 1) {
                    System.err.println("Received a differential frame before a full frame. Skipping.");
                    return;
                }
                if (screenBuffer == null || frameType == 2) {
                    // Note: A full frame might not be the full screen size if scaled.
                    // We create the buffer based on the first full frame's dimensions.
                    if (screenBuffer == null || screenBuffer.getWidth() != width || screenBuffer.getHeight() != height) {
                         screenBuffer = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
                    }
                }

                if (screenBuffer != null) {
                    Graphics2D g = screenBuffer.createGraphics();
                    g.drawImage(diffImage, x, y, null);
                    g.dispose();
                }

            } catch (IOException e) {
                System.err.println("Failed to decode screen frame: " + e.getMessage());
            }
        }

        public void updateCursor(byte[] pngData, int x, int y, boolean isVisible, boolean isMonochrome) {
            this.cursorX = x;
            this.cursorY = y;
            this.isCursorVisible = isVisible;
            this.isCursorMonochrome = isMonochrome;

            // Only decode a new image if one was sent
            if (pngData != null && pngData.length > 0) {
                try {
                    this.cursorImage = ImageIO.read(new ByteArrayInputStream(pngData));
                } catch (IOException e) {
                    System.err.println("Failed to decode cursor image: " + e.getMessage());
                    this.cursorImage = null; // Invalidate cursor on decode error
                }
            }
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            Graphics2D g2d = (Graphics2D) g;

            // Fill background
            g2d.setColor(Color.BLACK);
            g2d.fillRect(0, 0, getWidth(), getHeight());

            // Draw the screen buffer
            if (screenBuffer != null) {
                int panelWidth = getWidth();
                int panelHeight = getHeight();
                int bufferWidth = screenBuffer.getWidth();
                int bufferHeight = screenBuffer.getHeight();

                // Calculate the best-fit dimensions while maintaining aspect ratio
                double panelRatio = (double) panelWidth / panelHeight;
                double bufferRatio = (double) bufferWidth / bufferHeight;

                int scaledWidth;
                int scaledHeight;

                if (panelRatio > bufferRatio) { // Panel is wider than the image
                    scaledHeight = panelHeight;
                    scaledWidth = (int) (scaledHeight * bufferRatio);
                } else { // Panel is taller than or same ratio as the image
                    scaledWidth = panelWidth;
                    scaledHeight = (int) (scaledWidth / bufferRatio);
                }

                // Center the scaled image
                int drawX = (panelWidth - scaledWidth) / 2;
                int drawY = (panelHeight - scaledHeight) / 2;

                // Set rendering hints for high-quality image scaling.
                // Without this, the default (often nearest-neighbor) scaling can look blurry or pixelated.
                g2d.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR);

                g2d.drawImage(screenBuffer, drawX, drawY, scaledWidth, scaledHeight, this);

                // Draw the cursor on top of the screen buffer
                if (isCursorVisible && cursorImage != null) {
                    if (isCursorMonochrome) {
                        // For monochrome cursors, render with inverted colors (XOR effect).
                        int cursorImgWidth = cursorImage.getWidth();
                        int cursorImgHeight = cursorImage.getHeight();
                        
                        // Pre-calculate scaling factors outside the loop for efficiency.
                        double scaleX = (double) scaledWidth / bufferWidth;
                        double scaleY = (double) scaledHeight / bufferHeight;

                        for (int cy = 0; cy < cursorImgHeight; cy++) {
                            for (int cx = 0; cx < cursorImgWidth; cx++) {
                                // The received monochrome cursor image has non-transparent pixels for its shape.
                                if ((cursorImage.getRGB(cx, cy) >> 24) != 0x00) { // Check if not transparent
                                    int screenX = cursorX + cx;
                                    int screenY = cursorY + cy;
                                    // Ensure the pixel is within the screen buffer bounds.
                                    if (screenX >= 0 && screenX < bufferWidth && screenY >= 0 && screenY < bufferHeight) {
                                        int originalRGB = screenBuffer.getRGB(screenX, screenY);
                                        int invertedRGB = (~originalRGB) | 0xFF000000; // Invert color and ensure it's opaque.
                                        g2d.setColor(new Color(invertedRGB, true));

                                        int finalX = drawX + (int) (screenX * scaleX);
                                        int finalY = drawY + (int) (screenY * scaleY);
                                        g2d.fillRect(finalX, finalY, 1, 1);
                                    }
                                }
                            }
                        }
                    } else {
                        // For regular (color) cursors, just draw the image.
                        double scaleX = (double) scaledWidth / bufferWidth;
                        double scaleY = (double) scaledHeight / bufferHeight;
                        g2d.drawImage(cursorImage, drawX + (int)(cursorX * scaleX), drawY + (int)(cursorY * scaleY), (int)(cursorImage.getWidth() * scaleX), (int)(cursorImage.getHeight() * scaleY), this);
                    }
                }
            } else {
                // If no frame received yet, show a message
                g2d.setColor(Color.WHITE);
                g2d.drawString("Waiting for first frame from server...", 50, 50);
            }
        }
    }
}
