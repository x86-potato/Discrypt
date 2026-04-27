#pragma once
#include <vector>
#include <string>
#include <bcrypt.h>

namespace Discrypt
{
	/// <summary>
	/// Manages encryption session state and cryptographic operations
	/// </summary>
	struct EncryptionSession
	{
		BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
		BCRYPT_KEY_HANDLE hPrivateKey = nullptr;
		BCRYPT_SECRET_HANDLE hSharedSecret = nullptr;
		std::vector<BYTE> publicKeyBlob;
		std::vector<BYTE> partnerPublicKeyBlob;
		std::vector<BYTE> sharedSecretData;
		enum class State { NoHandshake, HandshakeInitiated, HandshakeComplete } state = State::NoHandshake;
	};

	/// <summary>
	/// Handles all cryptographic operations for the application
	/// </summary>
	class CryptoManager
	{
	public:
		// Base64 encoding/decoding
		static std::wstring Base64Encode(const std::vector<BYTE>& data);
		static std::vector<BYTE> Base64Decode(const std::wstring& base64);

		// Key generation and exchange
		static bool GenerateDHKeyPair(EncryptionSession& session);
		static bool DeriveSharedSecret(EncryptionSession& session, const std::vector<BYTE>& partnerPublicKeyBlob);

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
