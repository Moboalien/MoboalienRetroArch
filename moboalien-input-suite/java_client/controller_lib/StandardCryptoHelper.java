package java_client.controller_lib;

import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

public class StandardCryptoHelper implements ICryptoHelper {
    private static final String ALGORITHM = "AES";
    private static final String TRANSFORMATION = "AES/CBC/PKCS5Padding";
    private static final byte[] ZERO_IV = new byte[16];

    @Override
    public byte[] AESEncrypt(byte[] data, byte[] key) throws Exception {
        // 1. Setup AES/CBC/PKCS5Padding
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        
        // 2. Define a constant all-zero IV (16 bytes)
        IvParameterSpec iv = new IvParameterSpec(ZERO_IV);
        
        // 3. Initialize and Encrypt
        SecretKeySpec skeySpec = new SecretKeySpec(key, ALGORITHM);
        cipher.init(Cipher.ENCRYPT_MODE, skeySpec, iv);
        byte[] encrypted = cipher.doFinal(data);
        return encrypted;
    }

    @Override
    public byte[] AESDecrypt(byte[] data, byte[] key) throws Exception {
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        // 2. Define a constant all-zero IV (16 bytes)
        IvParameterSpec iv = new IvParameterSpec(ZERO_IV);
        // 3. Initialize and Encrypt
        SecretKeySpec skeySpec = new SecretKeySpec(key, ALGORITHM);
        cipher.init(Cipher.DECRYPT_MODE, skeySpec, iv);
        return cipher.doFinal(data);
    }
}