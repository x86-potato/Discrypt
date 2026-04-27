#include "pch.h"
#include "CryptoManager.h"
#include <wincrypt.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "crypt32.lib")

namespace Discrypt
{
	std::wstring CryptoManager::Base64Encode(const std::vector<BYTE>& data)
	{
		if (data.empty()) return L"";

		DWORD base64Length = 0;
		CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &base64Length);

		std::wstring base64(base64Length, L'\0');
		CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64.data(), &base64Length);

		// Remove null terminator if present
		if (!base64.empty() && base64.back() == L'\0')
			base64.pop_back();

		return base64;
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
		OutputDebugStringW(L"[Discrypt] Generating ECDH key pair...\n");

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
			OutputDebugStringW(L"[Discrypt] Failed to open ECDH algorithm provider\n");
			return false;
		}

		// Generate key pair
		status = BCryptGenerateKeyPair(session.hAlgorithm, &session.hPrivateKey, 256, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
			BCryptCloseAlgorithmProvider(session.hAlgorithm, 0);
			session.hAlgorithm = nullptr;
			return false;
		}

		// Finalize the key pair
		status = BCryptFinalizeKeyPair(session.hPrivateKey, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to finalize key pair\n");
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
			OutputDebugStringW(L"[Discrypt] Failed to get public key size\n");
			return false;
		}

		session.publicKeyBlob.resize(publicKeySize);
		status = BCryptExportKey(session.hPrivateKey, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			session.publicKeyBlob.data(), publicKeySize, &publicKeySize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to export public key\n");
			return false;
		}

		OutputDebugStringW(L"[Discrypt] ECDH key pair generated successfully!\n");
		OutputDebugStringW((L"[Discrypt] Public key size: " + std::to_wstring(publicKeySize) + L" bytes\n").c_str());

		return true;
	}

	bool CryptoManager::DeriveSharedSecret(EncryptionSession& session, const std::vector<BYTE>& partnerPublicKeyBlob)
	{
		OutputDebugStringW(L"[Discrypt] Deriving shared secret...\n");

		if (!session.hAlgorithm || !session.hPrivateKey)
		{
			OutputDebugStringW(L"[Discrypt] No private key available for secret derivation\n");
			return false;
		}

		// Import partner's public key
		BCRYPT_KEY_HANDLE hPartnerPublicKey = nullptr;
		NTSTATUS status = BCryptImportKeyPair(session.hAlgorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			&hPartnerPublicKey, const_cast<PUCHAR>(partnerPublicKeyBlob.data()),
			static_cast<ULONG>(partnerPublicKeyBlob.size()), 0);

		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to import partner's public key\n");
			OutputDebugStringW((L"[Discrypt] NTSTATUS error: 0x" + 
				std::to_wstring(status) + L"\n").c_str());
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
			OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
			OutputDebugStringW((L"[Discrypt] NTSTATUS error: 0x" + 
				std::to_wstring(status) + L"\n").c_str());
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
			OutputDebugStringW(L"[Discrypt] Failed to get shared secret size\n");
			OutputDebugStringW((L"[Discrypt] NTSTATUS error: 0x" + 
				std::to_wstring(status) + L"\n").c_str());
			return false;
		}

		session.sharedSecretData.resize(secretSize);
		status = BCryptDeriveKey(session.hSharedSecret, BCRYPT_KDF_HASH, &kdfParams,
			session.sharedSecretData.data(), secretSize, &secretSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to derive shared secret data\n");
			OutputDebugStringW((L"[Discrypt] NTSTATUS error: 0x" + 
				std::to_wstring(status) + L"\n").c_str());
			return false;
		}

		OutputDebugStringW(L"[Discrypt] Shared secret derived successfully!\n");
		OutputDebugStringW((L"[Discrypt] Shared secret size: " + std::to_wstring(secretSize) + L" bytes\n").c_str());

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
		OutputDebugStringW((L"[Discrypt] GetSharedSecretHex called, data size: " + 
			std::to_wstring(session.sharedSecretData.size()) + L" bytes\n").c_str());

		if (session.sharedSecretData.empty())
		{
			OutputDebugStringW(L"[Discrypt] GetSharedSecretHex: No shared secret data!\n");
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
		session.state = EncryptionSession::State::NoHandshake;
	}
}
