#pragma once

// Exclude rarely-used stuff from Windows headers to speed up compilation
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// windows.h MUST come before the cryptography headers
#include <windows.h> 
#include <wincrypt.h>
#include <bcrypt.h>

#include <vector>
#include <string>

namespace Discrypt
{
    using byte = unsigned char;

    /// <summary>
    /// Manages encryption session state and cryptographic operations
    /// </summary>
    struct EncryptionSession
    {
        BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
        BCRYPT_KEY_HANDLE hPrivateKey = nullptr;
        BCRYPT_SECRET_HANDLE hSharedSecret = nullptr;

        std::vector<byte> publicKeyBlob;
        std::vector<byte> partnerPublicKeyBlob;
        std::vector<byte> sharedSecretData;

        enum class State
        {
            NoHandshake,
            HandshakeInitiated,
            HandshakeComplete
        } state = State::NoHandshake;
    };

    /// <summary>
    /// Handles all cryptographic operations for the application
    /// </summary>
    class CryptoManager
    {
    public:
        // Base64 encoding/decoding
        static std::wstring Base64Encode(const std::vector<byte>& data);
        static std::vector<byte> Base64Decode(const std::wstring& base64);

        // Key generation and exchange
        static bool GenerateDHKeyPair(EncryptionSession& session);
        static bool DeriveSharedSecret(EncryptionSession& session, const std::vector<byte>& partnerPublicKeyBlob);
        static std::vector<byte> ExportPrivateKeyBlob(const EncryptionSession& session);
        static bool ImportPrivateKey(EncryptionSession& session, const std::vector<byte>& privateKeyBlob);

        // Display helpers
        static std::wstring GetPublicKeyHex(const EncryptionSession& session);
        static std::wstring GetSharedSecretHex(const EncryptionSession& session);

        // Message encryption/decryption
        static std::wstring EncryptMessage(const EncryptionSession& session, const std::wstring& plaintext);
        static std::wstring DecryptMessage(const EncryptionSession& session, const std::wstring& ciphertext);

        // Cleanup
        static void CleanupSession(EncryptionSession& session);
    };
}