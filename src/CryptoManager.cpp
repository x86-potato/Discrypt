//#include "pch.h" // Uncomment this only if your VS project explicitly enforces Precompiled Headers
#include "CryptoManager.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm> // For std::min

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib") // Required for BCRYPT functions!

namespace Discrypt
{
	std::wstring CryptoManager::Base64Encode(const std::vector<BYTE>& data)
	{
		if (data.empty()) return L"";

		// Get required buffer size (includes null terminator)
		DWORD base64Length = 0;
		if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &base64Length))
		{
			std::wcout << L"[Discrypt] Base64Encode: Failed to get buffer size\n";
			return L"";
		}

		// Allocate buffer with exact size needed
		std::vector<wchar_t> buffer(base64Length);
		DWORD actualLength = base64Length;

		if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, buffer.data(), &actualLength))
		{
			std::wcout << L"[Discrypt] Base64Encode: Failed to encode\n";
			return L"";
		}

		// Create string from buffer, excluding null terminator if present
		std::wstring result(buffer.data(), actualLength > 0 && buffer[actualLength - 1] == L'\0' ? actualLength - 1 : actualLength);
		return result;
	}

	std::vector<BYTE> CryptoManager::Base64Decode(const std::wstring& base64)
	{
		if (base64.empty()) return {};

		DWORD dataLength = 0;
		CryptStringToBinaryW(base64.c_str(), 0, CRYPT_STRING_BASE64,
			nullptr, &dataLength, nullptr, nullptr);

		std::vector<BYTE> data(dataLength);
		CryptStringToBinaryW(base64.c_str(), 0, CRYPT_STRING_BASE64,
			data.data(), &dataLength, nullptr, nullptr);

		return data;
	}

	bool CryptoManager::GenerateDHKeyPair(EncryptionSession& session)
	{
		std::wcout << L"[Discrypt] Generating ECDH key pair...\n";

		// Clean up existing session
		if (session.hPrivateKey)
		{
			BCryptDestroyKey(session.hPrivateKey);
			session.hPrivateKey = nullptr;
		}
		if (session.hSharedSecret)
		{
			BCryptDestroySecret(session.hSharedSecret);
			session.hSharedSecret = nullptr;
		}
		if (session.hAlgorithm)
		{
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hAlgorithm = nullptr;
		}

		// Open ECDH algorithm provider
		NTSTATUS status = BCryptOpenAlgorithmProvider(&session.hAlgorithm,
			BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to open ECDH algorithm provider\n";
			return false;
		}

		// Generate key pair
		status = BCryptGenerateKeyPair(session.hAlgorithm, &session.hPrivateKey, 256, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to generate key pair\n";
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hAlgorithm = nullptr;
			return false;
		}

		// Finalize the key pair
		status = BCryptFinalizeKeyPair(session.hPrivateKey, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to finalize key pair\n";
			BCryptDestroyKey(session.hPrivateKey);
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hPrivateKey = nullptr;
			session.hAlgorithm = nullptr;
			return false;
		}

		// Export public key
		DWORD publicKeySize = 0;
		status = BCryptExportKey(session.hPrivateKey, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			nullptr, 0, &publicKeySize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to get public key size\n";
			return false;
		}

		session.publicKeyBlob.resize(publicKeySize);
		status = BCryptExportKey(session.hPrivateKey, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			session.publicKeyBlob.data(), publicKeySize, &publicKeySize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to export public key\n";
			return false;
		}

		std::wcout << L"[Discrypt] ECDH key pair generated successfully!\n";
		std::wcout << L"[Discrypt] Public key size: " << publicKeySize << L" bytes\n";

		return true;
	}

	bool CryptoManager::DeriveSharedSecret(EncryptionSession& session, const std::vector<BYTE>& partnerPublicKeyBlob)
	{
		std::wcout << L"[Discrypt] Deriving shared secret...\n";

		if (!session.hAlgorithm || !session.hPrivateKey)
		{
			std::wcout << L"[Discrypt] No private key available for secret derivation\n";
			return false;
		}

		// Import partner's public key
		BCRYPT_KEY_HANDLE hPartnerPublicKey = nullptr;
		NTSTATUS status = BCryptImportKeyPair(session.hAlgorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			&hPartnerPublicKey, const_cast<PUCHAR>(partnerPublicKeyBlob.data()),
			static_cast<ULONG>(partnerPublicKeyBlob.size()), 0);

		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to import partner's public key\n";
			std::wcout << L"[Discrypt] NTSTATUS error: 0x" << std::hex << status << std::dec << L"\n";
			return false;
		}

		// Clean up old shared secret if exists
		if (session.hSharedSecret)
		{
			BCryptDestroySecret(session.hSharedSecret);
			session.hSharedSecret = nullptr;
		}

		// Derive shared secret
		status = BCryptSecretAgreement(session.hPrivateKey, hPartnerPublicKey,
			&session.hSharedSecret, 0);

		BCryptDestroyKey(hPartnerPublicKey);

		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to derive shared secret\n";
			std::wcout << L"[Discrypt] NTSTATUS error: 0x" << std::hex << status << std::dec << L"\n";
			return false;
		}

		// Set up KDF parameters for SHA256 hash
		BCryptBuffer kdfBuffer;
		kdfBuffer.BufferType = KDF_HASH_ALGORITHM;
		kdfBuffer.cbBuffer = sizeof(BCRYPT_SHA256_ALGORITHM);
		kdfBuffer.pvBuffer = (PVOID)BCRYPT_SHA256_ALGORITHM;

		BCryptBufferDesc kdfParams;
		kdfParams.ulVersion = BCRYPTBUFFER_VERSION;
		kdfParams.cBuffers = 1;
		kdfParams.pBuffers = &kdfBuffer;

		// Derive raw secret bytes with SHA256
		DWORD secretSize = 0;
		status = BCryptDeriveKey(session.hSharedSecret, BCRYPT_KDF_HASH, &kdfParams,
			nullptr, 0, &secretSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to get shared secret size\n";
			std::wcout << L"[Discrypt] NTSTATUS error: 0x" << std::hex << status << std::dec << L"\n";
			return false;
		}

		session.sharedSecretData.resize(secretSize);
		status = BCryptDeriveKey(session.hSharedSecret, BCRYPT_KDF_HASH, &kdfParams,
			session.sharedSecretData.data(), secretSize, &secretSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to derive shared secret data\n";
			std::wcout << L"[Discrypt] NTSTATUS error: 0x" << std::hex << status << std::dec << L"\n";
			return false;
		}

		std::wcout << L"[Discrypt] Shared secret derived successfully!\n";
		std::wcout << L"[Discrypt] Shared secret size: " << secretSize << L" bytes\n";

		return true;
	}

	std::vector<BYTE> CryptoManager::ExportPrivateKeyBlob(const EncryptionSession& session)
	{
		if (!session.hPrivateKey)
		{
			std::wcout << L"[Discrypt] ExportPrivateKeyBlob: no private key handle\n";
			return {};
		}

		DWORD blobSize = 0;
		NTSTATUS status = BCryptExportKey(session.hPrivateKey, nullptr, BCRYPT_ECCPRIVATE_BLOB,
			nullptr, 0, &blobSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] ExportPrivateKeyBlob: failed to get blob size\n";
			return {};
		}

		std::vector<BYTE> blob(blobSize);
		status = BCryptExportKey(session.hPrivateKey, nullptr, BCRYPT_ECCPRIVATE_BLOB,
			blob.data(), blobSize, &blobSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] ExportPrivateKeyBlob: failed to export\n";
			return {};
		}

		std::wcout << L"[Discrypt] Private key blob exported: " << blobSize << L" bytes\n";
		return blob;
	}

	bool CryptoManager::ImportPrivateKey(EncryptionSession& session, const std::vector<BYTE>& privateKeyBlob)
	{
		if (privateKeyBlob.empty())
		{
			std::wcout << L"[Discrypt] ImportPrivateKey: empty blob\n";
			return false;
		}

		if (session.hPrivateKey) { BCryptDestroyKey(session.hPrivateKey); session.hPrivateKey = nullptr; }
		if (session.hSharedSecret) { BCryptDestroySecret(session.hSharedSecret); session.hSharedSecret = nullptr; }
		if (session.hAlgorithm) { BCryptCloseAlgorithmProvider(session.hAlgorithm, 0); session.hAlgorithm = nullptr; }

		NTSTATUS status = BCryptOpenAlgorithmProvider(&session.hAlgorithm,
			BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] ImportPrivateKey: failed to open algorithm provider\n";
			return false;
		}

		status = BCryptImportKeyPair(session.hAlgorithm, nullptr, BCRYPT_ECCPRIVATE_BLOB,
			&session.hPrivateKey,
			const_cast<PUCHAR>(privateKeyBlob.data()),
			static_cast<ULONG>(privateKeyBlob.size()), 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] ImportPrivateKey: failed to import key pair\n";
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hAlgorithm = nullptr;
			return false;
		}

		std::wcout << L"[Discrypt] Private key imported successfully\n";
		return true;
	}

	std::wstring CryptoManager::GetPublicKeyHex(const EncryptionSession& session)
	{
		if (session.publicKeyBlob.empty())
			return L"No public key generated";

		std::wstringstream ss;
		ss << std::hex << std::setfill(L'0');
		for (size_t i = 0; i < session.publicKeyBlob.size() && i < 32; ++i)
		{
			ss << std::setw(2) << session.publicKeyBlob[i];
			if (i < 31 && i < session.publicKeyBlob.size() - 1) ss << L":";
		}
		if (session.publicKeyBlob.size() > 32)
			ss << L"...";
		return ss.str();
	}

	std::wstring CryptoManager::GetSharedSecretHex(const EncryptionSession& session)
	{
		std::wcout << L"[Discrypt] GetSharedSecretHex called, data size: " << session.sharedSecretData.size() << L" bytes\n";

		if (session.sharedSecretData.empty())
		{
			std::wcout << L"[Discrypt] GetSharedSecretHex: No shared secret data!\n";
			return L"No shared secret";
		}

		std::wstringstream ss;
		ss << std::hex << std::setfill(L'0');
		for (size_t i = 0; i < session.sharedSecretData.size() && i < 16; ++i)
		{
			ss << std::setw(2) << session.sharedSecretData[i];
			if (i < 15 && i < session.sharedSecretData.size() - 1) ss << L":";
		}
		if (session.sharedSecretData.size() > 16)
			ss << L"...";
		return ss.str();
	}

	std::wstring CryptoManager::EncryptMessage(const EncryptionSession& session, const std::wstring& plaintext)
	{
		std::wcout << L"[Discrypt] Encrypting message with AES-GCM...\n";

		if (session.sharedSecretData.empty())
		{
			std::wcout << L"[Discrypt] No shared secret available for encryption\n";
			return L"";
		}

		// Convert plaintext to UTF-8 bytes
		int utf8Size = WideCharToMultiByte(CP_UTF8, 0, plaintext.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8Size <= 1) // Empty or error (size 1 = just null terminator)
		{
			std::wcout << L"[Discrypt] Empty plaintext or conversion error\n";
			return L"";
		}

		std::vector<BYTE> plaintextBytes(utf8Size - 1); // -1 to exclude null terminator
		// Don't pass -1 for length, use explicit length of buffer we allocated
		int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, plaintext.c_str(), static_cast<int>(plaintext.length()),
			(LPSTR)plaintextBytes.data(), static_cast<int>(plaintextBytes.size()), nullptr, nullptr);

		if (bytesWritten <= 0)
		{
			std::wcout << L"[Discrypt] UTF-8 conversion failed\n";
			return L"";
		}

		// Adjust size to actual bytes written (should match, but be safe)
		plaintextBytes.resize(bytesWritten);

		// Open AES algorithm provider
		BCRYPT_ALG_HANDLE hAesAlg = nullptr;
		NTSTATUS status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to open AES algorithm\n";
			return L"";
		}

		// Set chaining mode to GCM
		status = BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE,
			(PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to set GCM mode\n";
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"";
		}

		// Use first 32 bytes of shared secret as AES-256 key
		std::vector<BYTE> aesKey(32);
		size_t keySize = std::min<size_t>(32, session.sharedSecretData.size());
		memcpy(aesKey.data(), session.sharedSecretData.data(), keySize);
		if (keySize < 32)
		{
			// Pad with zeros if shared secret is smaller
			memset(aesKey.data() + keySize, 0, 32 - keySize);
		}

		// Import key
		BCRYPT_KEY_HANDLE hKey = nullptr;
		status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, nullptr, 0,
			aesKey.data(), static_cast<ULONG>(aesKey.size()), 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to import AES key\n";
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"";
		}

		// Generate random 12-byte IV (nonce) for GCM
		std::vector<BYTE> iv(12);
		BCryptGenRandom(nullptr, iv.data(), static_cast<ULONG>(iv.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

		// Prepare GCM authentication info
		BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
		BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
		authInfo.pbNonce = iv.data();
		authInfo.cbNonce = static_cast<ULONG>(iv.size());

		std::vector<BYTE> tag(16); // 16-byte authentication tag
		authInfo.pbTag = tag.data();
		authInfo.cbTag = static_cast<ULONG>(tag.size());
		authInfo.pbAuthData = nullptr;
		authInfo.cbAuthData = 0;

		// Get ciphertext size
		DWORD ciphertextSize = 0;
		status = BCryptEncrypt(hKey, plaintextBytes.data(), static_cast<ULONG>(plaintextBytes.size()),
			&authInfo, nullptr, 0, nullptr, 0, &ciphertextSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to get ciphertext size\n";
			BCryptDestroyKey(hKey);
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"";
		}

		// Encrypt
		std::vector<BYTE> ciphertext(ciphertextSize);
		status = BCryptEncrypt(hKey, plaintextBytes.data(), static_cast<ULONG>(plaintextBytes.size()),
			&authInfo, nullptr, 0, ciphertext.data(), ciphertextSize, &ciphertextSize, 0);

		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAesAlg, 0);

		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Encryption failed\n";
			return L"";
		}

		// Combine: IV (12 bytes) + Ciphertext + Tag (16 bytes)
		// Reserve capacity to avoid reallocations
		std::vector<BYTE> combined;
		combined.reserve(iv.size() + ciphertext.size() + tag.size());
		combined.insert(combined.end(), iv.begin(), iv.end());
		combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
		combined.insert(combined.end(), tag.begin(), tag.end());

		// Base64 encode and add prefix
		std::wstring encoded = Base64Encode(combined);
		std::wcout << L"[Discrypt] Message encrypted successfully (" << combined.size() << L" bytes)\n";

		return L"[ENC]:" + encoded;
	}

	std::wstring CryptoManager::DecryptMessage(const EncryptionSession& session, const std::wstring& ciphertext)
	{
		std::wcout << L"[Discrypt] Decrypting message with AES-GCM...\n";

		if (session.sharedSecretData.empty())
		{
			std::wcout << L"[Discrypt] No shared secret available for decryption\n";
			return L"[DECRYPT FAILED: No shared secret]";
		}

		// Remove [ENC]: prefix
		std::wstring encoded = ciphertext;
		if (encoded.find(L"[ENC]:") == 0)
		{
			encoded = encoded.substr(6);
		}

		// Base64 decode
		std::vector<BYTE> combined = Base64Decode(encoded);
		if (combined.size() < 28) // At least IV(12) + Tag(16) = 28 bytes
		{
			std::wcout << L"[Discrypt] Ciphertext too short\n";
			return L"[DECRYPT FAILED: Invalid format]";
		}

		// Extract IV, ciphertext, and tag
		std::vector<BYTE> iv(combined.begin(), combined.begin() + 12);
		std::vector<BYTE> tag(combined.end() - 16, combined.end());
		std::vector<BYTE> encryptedData(combined.begin() + 12, combined.end() - 16);

		// Open AES algorithm provider
		BCRYPT_ALG_HANDLE hAesAlg = nullptr;
		NTSTATUS status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to open AES algorithm\n";
			return L"[DECRYPT FAILED]";
		}

		// Set chaining mode to GCM
		status = BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE,
			(PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to set GCM mode\n";
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"[DECRYPT FAILED]";
		}

		// Use first 32 bytes of shared secret as AES-256 key
		std::vector<BYTE> aesKey(32);
		size_t keySize = std::min<size_t>(32, session.sharedSecretData.size());
		memcpy(aesKey.data(), session.sharedSecretData.data(), keySize);
		if (keySize < 32)
		{
			memset(aesKey.data() + keySize, 0, 32 - keySize);
		}

		// Import key
		BCRYPT_KEY_HANDLE hKey = nullptr;
		status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, nullptr, 0,
			aesKey.data(), static_cast<ULONG>(aesKey.size()), 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to import AES key\n";
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"[DECRYPT FAILED]";
		}

		// Prepare GCM authentication info
		BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
		BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
		authInfo.pbNonce = iv.data();
		authInfo.cbNonce = static_cast<ULONG>(iv.size());
		authInfo.pbTag = tag.data();
		authInfo.cbTag = static_cast<ULONG>(tag.size());
		authInfo.pbAuthData = nullptr;
		authInfo.cbAuthData = 0;

		// Get plaintext size
		DWORD plaintextSize = 0;
		status = BCryptDecrypt(hKey, encryptedData.data(), static_cast<ULONG>(encryptedData.size()),
			&authInfo, nullptr, 0, nullptr, 0, &plaintextSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Failed to get plaintext size\n";
			BCryptDestroyKey(hKey);
			BCryptCloseAlgorithmProvider(hAesAlg, 0);
			return L"[DECRYPT FAILED]";
		}

		// Decrypt
		std::vector<BYTE> plaintext(plaintextSize);
		status = BCryptDecrypt(hKey, encryptedData.data(), static_cast<ULONG>(encryptedData.size()),
			&authInfo, nullptr, 0, plaintext.data(), plaintextSize, &plaintextSize, 0);

		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAesAlg, 0);

		if (!BCRYPT_SUCCESS(status))
		{
			std::wcout << L"[Discrypt] Decryption failed (authentication may have failed)\n";
			return L"[DECRYPT FAILED: Invalid key or corrupted message]";
		}

		// Convert UTF-8 bytes back to wide string
		int wideSize = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)plaintext.data(), plaintextSize, nullptr, 0);
		if (wideSize <= 0)
		{
			std::wcout << L"[Discrypt] Failed to calculate wide string size\n";
			return L"[DECRYPT FAILED: Conversion error]";
		}

		std::vector<wchar_t> wideBuffer(wideSize);
		int charsWritten = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)plaintext.data(), plaintextSize, wideBuffer.data(), wideSize);

		if (charsWritten <= 0)
		{
			std::wcout << L"[Discrypt] Failed to convert UTF-8 to wide string\n";
			return L"[DECRYPT FAILED: Conversion error]";
		}

		std::wstring result(wideBuffer.data(), charsWritten);

		std::wcout << L"[Discrypt] Message decrypted successfully: " << result << L"\n";
		return result;
	}

	void CryptoManager::CleanupSession(EncryptionSession& session)
	{
		if (session.hPrivateKey)
		{
			BCryptDestroyKey(session.hPrivateKey);
			session.hPrivateKey = nullptr;
		}
		if (session.hSharedSecret)
		{
			BCryptDestroySecret(session.hSharedSecret);
			session.hSharedSecret = nullptr;
		}
		if (session.hAlgorithm)
		{
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hAlgorithm = nullptr;
		}

		session.publicKeyBlob.clear();
		session.partnerPublicKeyBlob.clear();
		session.sharedSecretData.clear();

		// Note: Ensure EncryptionSession::State enum exists in CryptoManager.h. 
		// If it was removed, comment out the line below.
		// session.state = EncryptionSession::State::NoHandshake; 
	}
}