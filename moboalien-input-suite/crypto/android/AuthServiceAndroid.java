package auth.android;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;

public class AuthServiceAndroid implements IAuthService {
    private static final String RSA_ALGO = "RSA/ECB/PKCS1Padding";
    private static final String AES_ALGO = "AES/CBC/PKCS5Padding";

    @Override
    public RSAKeyPair GenerateRSAKeyPair() throws Exception {
        KeyPairGenerator kpg = KeyPairGenerator.getInstance("RSA");
        kpg.initialize(2048);
        KeyPair kp = kpg.generateKeyPair();
        RSAKeyPair out = new RSAKeyPair();
        out.publicKey = kp.getPublic().getEncoded();
        out.privateKey = kp.getPrivate().getEncoded();
        return out;
    }

    @Override
    public AuthChallenge CreateChallenge(RSAKeyPair keyPair) throws Exception {
        AuthChallenge c = new AuthChallenge();
        c.challenge = GenerateRandomBytes(32);
        c.publicKey = keyPair.publicKey;
        return c;
    }

    @Override
    public boolean ValidateResponse(AuthResponse response, RSAKeyPair keyPair, byte[] challenge, String expectedPasswordHash) throws Exception {
        try {
            // decrypt with private key
            KeyFactory kf = KeyFactory.getInstance("RSA");
            PKCS8EncodedKeySpec privSpec = new PKCS8EncodedKeySpec(keyPair.privateKey);
            PrivateKey priv = kf.generatePrivate(privSpec);

            Cipher rsa = Cipher.getInstance(RSA_ALGO);
            rsa.init(Cipher.DECRYPT_MODE, priv);
            byte[] decrypted = rsa.doFinal(response.encryptedResponse);

            if (decrypted.length < 32) return false;
            byte[] recChallenge = new byte[32];
            System.arraycopy(decrypted, 0, recChallenge, 0, 32);
            int hashLen = decrypted.length - 32;
            byte[] recHash = new byte[hashLen];
            System.arraycopy(decrypted, 32, recHash, 0, hashLen);
            String recHashStr = new String(recHash, "UTF-8");

            // compare
            if (!java.util.Arrays.equals(recChallenge, challenge)) return false;
            if (!recHashStr.equals(expectedPasswordHash)) return false;

            // Authentication successful
            return true;
        } catch (Exception ex) {
            return false;
        }
    }

    @Override
    public AuthResponse CreateResponse(AuthChallenge challenge, String passwordHash) throws Exception {
        // prepare plaintext = challenge(32) + passwordHash bytes
        byte[] hashBytes = passwordHash.getBytes("UTF-8");
        byte[] plain = new byte[challenge.challenge.length + hashBytes.length];
        System.arraycopy(challenge.challenge, 0, plain, 0, challenge.challenge.length);
        System.arraycopy(hashBytes, 0, plain, challenge.challenge.length, hashBytes.length);

        // import server public key
        KeyFactory kf = KeyFactory.getInstance("RSA");
        X509EncodedKeySpec pubSpec = new X509EncodedKeySpec(challenge.publicKey);
        PublicKey pub = kf.generatePublic(pubSpec);

        Cipher rsa = Cipher.getInstance(RSA_ALGO);
        rsa.init(Cipher.ENCRYPT_MODE, pub);
        byte[] encrypted = rsa.doFinal(plain);

        AuthResponse r = new AuthResponse();
        r.encryptedResponse = encrypted;
        return r;
    }

    @Override
    public String HashText(String password) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] d = md.digest(password.getBytes("UTF-8"));
        StringBuilder sb = new StringBuilder();
        for (byte b : d) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    @Override
    public byte[] GenerateAESKey() throws Exception {
        KeyGenerator kg = KeyGenerator.getInstance("AES");
        kg.init(256);
        SecretKey sk = kg.generateKey();
        return sk.getEncoded();
    }

    @Override
    public byte[] AESEncrypt(byte[] data, byte[] key) throws Exception {
        SecureRandom rnd = new SecureRandom();
        byte[] iv = new byte[16];
        rnd.nextBytes(iv);
        SecretKeySpec ks = new SecretKeySpec(key, "AES");
        Cipher c = Cipher.getInstance(AES_ALGO);
        c.init(Cipher.ENCRYPT_MODE, ks, new IvParameterSpec(iv));
        byte[] ct = c.doFinal(data);
        byte[] out = new byte[iv.length + ct.length];
        System.arraycopy(iv, 0, out, 0, iv.length);
        System.arraycopy(ct, 0, out, iv.length, ct.length);
        return out;
    }

    @Override
    public byte[] AESDecrypt(byte[] encryptedData, byte[] key) throws Exception {
        if (encryptedData.length < 16) throw new IllegalArgumentException("Invalid data");
        byte[] iv = new byte[16];
        System.arraycopy(encryptedData, 0, iv, 0, 16);
        byte[] ct = new byte[encryptedData.length - 16];
        System.arraycopy(encryptedData, 16, ct, 0, ct.length);
        SecretKeySpec ks = new SecretKeySpec(key, "AES");
        Cipher c = Cipher.getInstance(AES_ALGO);
        c.init(Cipher.DECRYPT_MODE, ks, new IvParameterSpec(iv));
        return c.doFinal(ct);
    }

    @Override
    public byte[] GenerateRandomBytes(int length) throws Exception {
        byte[] b = new byte[length];
        SecureRandom sr = new SecureRandom();
        sr.nextBytes(b);
        return b;
    }
}
