Android Java Auth Service

Files:
- `IAuthService.java` - Java interface matching native IAuthService.
- `AuthServiceAndroid.java` - Android/JCE implementation using RSA (2048), AES-256-CBC, SHA-256.

Usage:
- Instantiate via `new AuthServiceAndroid()` or your DI container.
- Methods mirror the native C++ interface: `GenerateRSAKeyPair()`, `CreateChallenge()`, `CreateResponse()`, `ValidateResponse()`, `AESEncrypt()`, `AESDecrypt()`, `HashText()`.

Notes:
- Keys are returned/accepted as encoded byte arrays (X.509 for public, PKCS#8 for private).
- Ensure Android API level supports AES-256 (may require installing the proper crypto provider or using BouncyCastle on older devices).
- To use in Gradle project, add the `auth/android` sources as a module or copy the files into your Android app's package tree.

Quick test snippet (Java):

```java
IAuthService svc = new AuthServiceAndroid();
IAuthService.RSAKeyPair kp = svc.GenerateRSAKeyPair();
IAuthService.AuthChallenge ch = svc.CreateChallenge(kp);
String pwHash = svc.HashText("password123");
IAuthService.AuthResponse resp = svc.CreateResponse(ch, pwHash);
byte[] sessionKey = svc.ValidateResponse(resp, kp, ch.challenge, pwHash);
```

Security:
- This implementation uses standard JCE primitives. Use Android's secure keystore for long-term private key storage if needed.
