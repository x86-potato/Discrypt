# Diffie-Hellman Key Exchange Implementation

## Overview
Your Discrypt app now implements Elliptic Curve Diffie-Hellman (ECDH) key exchange using the Windows BCrypt API with the P-256 curve.

## How It Works

### 1. **Key Generation**
When you press **Alt+Enter** in Discord for the first time:
- Generates an ECDH P-256 key pair (private + public key)
- Private key stays on your computer (never transmitted)
- Public key is sent to your chat partner

### 2. **Handshake Protocol**

#### Scenario A: You Initiate
1. Press **Alt+Enter** with any text (or empty)
2. App sends: `HANDSHAKE_INIT:[your_public_key_base64]`
3. Partner receives it and responds: `HANDSHAKE_RESPONSE:[their_public_key_base64]`
4. You receive their response → both derive the same shared secret!

#### Scenario B: Partner Initiates
1. Partner sends: `HANDSHAKE_INIT:[their_public_key_base64]`
2. You type it in Discord and press **Alt+Enter**
3. App detects handshake, generates your keys, responds with: `HANDSHAKE_RESPONSE:[your_public_key_base64]`
4. Both sides now have shared secret!

### 3. **Shared Secret Derivation**
- Both parties compute: `shared_secret = DH(your_private_key, partner_public_key)`
- Magic of Diffie-Hellman: both get the **same secret** without ever sharing private keys!
- This shared secret will be used for AES encryption (coming next)

## Using The App

### Initial Setup
1. Open Discord
2. Run Discrypt app
3. Click "Refresh Keys" button to see current state

### Starting a Secure Chat
1. In Discord, press **Alt+Enter** (first time with no handshake)
2. Copy the `HANDSHAKE_INIT:...` message and send it
3. Wait for partner to respond with `HANDSHAKE_RESPONSE:...`
4. Copy their response into Discord and press **Alt+Enter**
5. Click "Refresh Keys" in Discrypt to see:
   - Your Public Key (first 32 bytes in hex)
   - Shared Secret (first 16 bytes in hex)

### After Handshake
- Any message you type + **Alt+Enter** will show `[ENCRYPTED]` prefix (placeholder for actual encryption)
- Both you and partner have the same shared secret for encrypting future messages

## Security Notes

### What's Secure:
✅ Private keys never leave your computer  
✅ Eavesdroppers can't compute shared secret from public keys  
✅ Forward secrecy (new handshake = new shared secret)  
✅ Uses industry-standard P-256 elliptic curve

### Current Limitations:
⚠️ Messages not actually encrypted yet (coming next)  
⚠️ No authentication (anyone can initiate handshake)  
⚠️ Session-only storage (restart = lose keys)  
⚠️ Single conversation at a time

## Technical Details

### Cryptography
- **Algorithm**: ECDH (Elliptic Curve Diffie-Hellman)
- **Curve**: NIST P-256 (secp256r1)
- **Key Size**: 256 bits
- **API**: Windows BCrypt (bcrypt.lib)

### Session State
```cpp
struct EncryptionSession {
    BCRYPT_ALG_HANDLE hAlgorithm;      // ECDH algorithm provider
    BCRYPT_KEY_HANDLE hPrivateKey;      // Your private key
    BCRYPT_SECRET_HANDLE hSharedSecret; // Shared secret handle
    std::vector<BYTE> publicKeyBlob;    // Your public key
    std::vector<BYTE> partnerPublicKeyBlob; // Partner's public key
    std::vector<BYTE> sharedSecretData; // Derived secret bytes
    State state; // NoHandshake, HandshakeInitiated, HandshakeComplete
};
```

### Key Functions
- `GenerateDHKeyPair()` - Creates ECDH key pair
- `DeriveSharedSecret()` - Computes shared secret from partner's public key
- `Base64Encode/Decode()` - Convert keys for Discord transmission
- `GetPublicKeyHex()` - Display key in GUI (first 32 bytes)
- `GetSharedSecretHex()` - Display secret in GUI (first 16 bytes)

## Next Steps
1. ✅ Key exchange implementation (DONE)
2. 🔄 Add AES-GCM encryption using shared secret
3. 🔄 Encrypt outgoing messages
4. 🔄 Monitor and decrypt incoming messages
5. 🔄 Add message authentication (HMAC or GCM tag)

## Debug Output
Watch Visual Studio Output window for logs:
```
[Discrypt] Alt+Enter pressed in Discord! Processing...
[Discrypt] Initiating handshake...
[Discrypt] Generating ECDH key pair...
[Discrypt] ECDH key pair generated successfully!
[Discrypt] Public key size: 72 bytes
[Discrypt] Public Key: 45:43:4b:31:20:00:00:00:3c:85:...
```

## Troubleshooting

**Q: "No key generated yet" in GUI**  
A: Press Alt+Enter in Discord once to initiate handshake

**Q: Keys not updating in GUI**  
A: Click "Refresh Keys" button after each handshake step

**Q: Handshake stuck at "initiated" state**  
A: Partner needs to send their HANDSHAKE_RESPONSE back

**Q: Both sides showing different shared secrets**  
A: Ensure you're both using the same handshake messages (copy/paste correctly)
