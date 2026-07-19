package auth.android;

import java.util.List;

public interface IAuthService {
    class RSAKeyPair {
        public byte[] publicKey;
        public byte[] privateKey;
    }

    class AuthChallenge {
        public byte[] challenge;
        public byte[] publicKey;
    }

    class AuthResponse {
        public byte[] encryptedResponse;
    }

    // Server-side
    RSAKeyPair GenerateRSAKeyPair() throws Exception;
    AuthChallenge CreateChallenge(RSAKeyPair keyPair) throws Exception;
    boolean ValidateResponse(AuthResponse response, RSAKeyPair keyPair, byte[] challenge, String expectedPasswordHash) throws Exception;

    // Client-side
    AuthResponse CreateResponse(AuthChallenge challenge, String passwordHash) throws Exception;

    // Utility
    String HashText(String password) throws Exception;
    byte[] GenerateAESKey() throws Exception;

    // AES
    byte[] AESEncrypt(byte[] data, byte[] key) throws Exception;
    byte[] AESDecrypt(byte[] encryptedData, byte[] key) throws Exception;

    // Random
    byte[] GenerateRandomBytes(int length) throws Exception;
}
