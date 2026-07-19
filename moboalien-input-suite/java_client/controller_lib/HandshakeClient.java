package java_client.controller_lib;

import java.io.IOException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;

public class HandshakeClient {
    private static final int TIMEOUT_MS = 2000;
    
    public static class HandshakeResult {
        public boolean success;
        public String message;
        public int controllerPort;
        public int screenPort;
        public long serverTicks;
        public PacketEncryptor encryptor;

        public HandshakeResult(boolean success, String message) {
            this.success = success;
            this.message = message;
        }
    }

    public HandshakeResult performHandshake(String serverIP, int handshakePort, String password) {
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setSoTimeout(TIMEOUT_MS);
            InetAddress serverAddress = InetAddress.getByName(serverIP);
            System.out.println("HandshakeClient: Sending Hello to " + serverAddress + ":" + handshakePort);

            // 1. Send Hello
            byte[] helloPacket = ControllerProtocol.createHelloPacket(1, 0);
            sendPacket(socket, helloPacket, serverAddress, handshakePort);

            // 2. Receive Hello Response
            byte[] buffer = new byte[1024];
            DatagramPacket responsePacket = new DatagramPacket(buffer, buffer.length);
            socket.receive(responsePacket);

            // Parse hello response using ControllerProtocol helper
            byte[] respData = new byte[responsePacket.getLength()];
            System.arraycopy(responsePacket.getData(), 0, respData, 0, responsePacket.getLength());
            ControllerProtocol.HelloResponse helloResp;
            try {
                helloResp = ControllerProtocol.parseHelloResponse(respData);
            } catch (IllegalArgumentException ex) {
                return new HandshakeResult(false, "Invalid hello response: " + ex.getMessage());
            }
            int controllerPort = helloResp.controllerPort;
            int screenPort = helloResp.screenPort;
            byte[] salt = helloResp.salt;

            // Log salt for verification
            System.out.println("Salt (utf8): " + new String(salt, StandardCharsets.UTF_8) + " controllerPort=" + controllerPort + " screenPort=" + screenPort);

            // 3. Send Auth Packet
            byte[] sessionKey = generateSessionKey(password, salt);
            ICryptoHelper cryptoHelper = new StandardCryptoHelper();
            
            // Create challenge payload: "MAGIC" + "Challenge123456"
            String payloadStr = "MAGIC" + "Challenge123456";
            byte[] payload = payloadStr.getBytes(StandardCharsets.UTF_8);
            
            // Encrypt challenge using PacketEncryptor helper (OpenSSL-compatible)
            byte[] encryptedChallenge = cryptoHelper.AESEncrypt(payload, sessionKey);
            System.out.println("Encrypted Challenge (hex): " + bytesToHex(encryptedChallenge));
            
            byte[] authPacket = ControllerProtocol.createAuthPacket(encryptedChallenge);
            sendPacket(socket, authPacket, serverAddress, handshakePort);

            // 4. Receive Auth Result
            socket.receive(responsePacket);
            ByteBuffer respBuffer = ByteBuffer.wrap(responsePacket.getData(), 0, responsePacket.getLength());
            respBuffer.order(ByteOrder.BIG_ENDIAN);

            byte type = respBuffer.get();
            if (type != ControllerProtocol.PACKET_AUTH_RESULT) {
                return new HandshakeResult(false, "Invalid auth result type: " + type);
            }

            byte result = respBuffer.get();
            long serverTicks = respBuffer.getLong();

            if (result != 1) {
                return new HandshakeResult(false, "Authentication failed (Server rejected password)");
            }

            // Handshake successful
            HandshakeResult res = new HandshakeResult(true, "Success");
            res.controllerPort = controllerPort;
            res.screenPort = screenPort;
            res.serverTicks = serverTicks;
            
            res.encryptor = new PacketEncryptor(cryptoHelper, sessionKey, serverTicks);
            return res;

        } catch (Exception e) {
            return new HandshakeResult(false, "Handshake exception: " + e.getMessage());
        }
    }

    private void sendPacket(DatagramSocket socket, byte[] data, InetAddress address, int port) throws IOException {
        DatagramPacket packet = new DatagramPacket(data, data.length, address, port);
        socket.send(packet);
    }

    private byte[] generateSessionKey(String password, byte[] salt) throws Exception {
        MessageDigest sha256 = MessageDigest.getInstance("SHA-256");
        
        // Step 1: Hash the password
        byte[] passHash = sha256.digest(password.getBytes(StandardCharsets.UTF_8));
        String passHashHex = bytesToHex(passHash);
        System.out.println("Password Hash: " + passHashHex);
        
        // Step 2: Decode salt bytes as UTF-8 string
        String saltString = new String(salt, StandardCharsets.UTF_8);
        System.out.println("Salt = " + saltString);
        
        // Step 3: Concatenate password hash (hex string) with salt string, then hash
        // This matches C++: HashText(passwordHash + saltString)
        String combined = passHashHex + saltString;
        byte[] keyHash = sha256.digest(combined.getBytes(StandardCharsets.UTF_8));
        
        // Step 4: Take first 16 bytes for the encryption key
        byte[] finalKey = Arrays.copyOf(keyHash, 16); // Truncate to 16 bytes (128-bit) for compatibility
        System.out.println("Generated Session Key: " + bytesToHex(finalKey));
        return finalKey;
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) {
            sb.append(String.format("%02x", b & 0xFF));
        }
        return sb.toString();
    }
}