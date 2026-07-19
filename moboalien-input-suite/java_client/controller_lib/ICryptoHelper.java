package java_client.controller_lib;

public interface ICryptoHelper {
    byte[] AESEncrypt(byte[] data, byte[] key) throws Exception;
    byte[] AESDecrypt(byte[] data, byte[] key) throws Exception;
}