package java_client.controller_lib;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

public class PacketEncryptor {
    private final byte[] encryptionKey;
    private final long timeOffset;
    private final ICryptoHelper cryptoHelper;

    /**
     * @param cryptoHelper  The crypto helper for encryption.
     * @param encryptionKey The session key derived from the handshake.
     * @param serverTicks   The server's tick count (system uptime in ms) at handshake time.
     */
    public PacketEncryptor(ICryptoHelper cryptoHelper, byte[] encryptionKey, long serverTicks) {
        this.cryptoHelper = cryptoHelper;
        this.encryptionKey = encryptionKey;
        // Use monotonic uptime (System.nanoTime) to avoid issues with wall-clock adjustments
        long clientUptimeMs = System.nanoTime() / 1_000_000L;
        this.timeOffset = serverTicks - clientUptimeMs;
        System.out.println("Time Offset (serverTicks - clientUptimeMs): " + timeOffset);
    }

    public byte[] encrypt(byte[] data) throws Exception {
        long timestamp = (System.nanoTime() / 1_000_000L) + timeOffset;
        System.err.println("Packet Timestamp (uptime ms): " + timestamp);
        byte[] plainText;
        try (ByteArrayOutputStream baos = new ByteArrayOutputStream();
             DataOutputStream dos = new DataOutputStream(baos)) {
            dos.writeLong(timestamp);
            dos.write(data);
            plainText = baos.toByteArray();
        } catch (IOException e) {
            throw new RuntimeException("Failed to create text input packet", e);
        }
        return cryptoHelper.AESEncrypt(plainText, encryptionKey);
    }
}