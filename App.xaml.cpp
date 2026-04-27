#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <bcrypt.h>
#include <wincrypt.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.


HWND Discord = nullptr; // Cached Discord window handle
HHOOK keyboardHook = nullptr;
std::wstring g_encryptionKey = L""; // Global storage for the password/key

// Encryption session state
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

EncryptionSession g_session;

namespace winrt::Discrypt::implementation
{
	/// <summary>
	/// Base64 encoding for transmitting keys
	/// </summary>
	std::wstring Base64Encode(const std::vector<BYTE>& data)
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

	/// <summary>
	/// Base64 decoding for receiving keys
	/// </summary>
	std::vector<BYTE> Base64Decode(const std::wstring& base64)
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

	/// <summary>
	/// Generate ECDH key pair for Diffie-Hellman exchange
	/// </summary>
	bool GenerateDHKeyPair()
	{
		OutputDebugStringW(L"[Discrypt] Generating ECDH key pair...\n");

		// Clean up existing session
		if (g_session.hPrivateKey)
		{
			BCryptDestroyKey(g_session.hPrivateKey);
			g_session.hPrivateKey = nullptr;
		}
		if (g_session.hSharedSecret)
		{
			BCryptDestroySecret(g_session.hSharedSecret);
			g_session.hSharedSecret = nullptr;
		}
		if (g_session.hAlgorithm)
		{
			BCryptCloseAlgorithmProvider(g_session.hAlgorithm, 0);
			g_session.hAlgorithm = nullptr;
		}

		// Open ECDH algorithm provider
		NTSTATUS status = BCryptOpenAlgorithmProvider(&g_session.hAlgorithm,
			BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to open ECDH algorithm provider\n");
			return false;
		}

		// Generate key pair
		status = BCryptGenerateKeyPair(g_session.hAlgorithm, &g_session.hPrivateKey, 256, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
			BCryptCloseAlgorithmProvider(g_session.hAlgorithm, 0);
			g_session.hAlgorithm = nullptr;
			return false;
		}

		// Finalize the key pair
		status = BCryptFinalizeKeyPair(g_session.hPrivateKey, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to finalize key pair\n");
			BCryptDestroyKey(g_session.hPrivateKey);
			BCryptCloseAlgorithmProvider(g_session.hAlgorithm, 0);
			g_session.hPrivateKey = nullptr;
			g_session.hAlgorithm = nullptr;
			return false;
		}

		// Export public key
		DWORD publicKeySize = 0;
		status = BCryptExportKey(g_session.hPrivateKey, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			nullptr, 0, &publicKeySize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to get public key size\n");
			return false;
		}

		g_session.publicKeyBlob.resize(publicKeySize);
		status = BCryptExportKey(g_session.hPrivateKey, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			g_session.publicKeyBlob.data(), publicKeySize, &publicKeySize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to export public key\n");
			return false;
		}

		OutputDebugStringW(L"[Discrypt] ECDH key pair generated successfully!\n");
		OutputDebugStringW((L"[Discrypt] Public key size: " + std::to_wstring(publicKeySize) + L" bytes\n").c_str());

		return true;
	}

	/// <summary>
	/// Derive shared secret from partner's public key
	/// </summary>
	bool DeriveSharedSecret(const std::vector<BYTE>& partnerPublicKeyBlob)
	{
		OutputDebugStringW(L"[Discrypt] Deriving shared secret...\n");

		if (!g_session.hAlgorithm || !g_session.hPrivateKey)
		{
			OutputDebugStringW(L"[Discrypt] No private key available for secret derivation\n");
			return false;
		}

		// Import partner's public key
		BCRYPT_KEY_HANDLE hPartnerPublicKey = nullptr;
		NTSTATUS status = BCryptImportKeyPair(g_session.hAlgorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB,
			&hPartnerPublicKey, const_cast<PUCHAR>(partnerPublicKeyBlob.data()),
			static_cast<ULONG>(partnerPublicKeyBlob.size()), 0);

		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to import partner's public key\n");
			return false;
		}

		// Clean up old shared secret if exists
		if (g_session.hSharedSecret)
		{
			BCryptDestroySecret(g_session.hSharedSecret);
			g_session.hSharedSecret = nullptr;
		}

		// Derive shared secret
		status = BCryptSecretAgreement(g_session.hPrivateKey, hPartnerPublicKey,
			&g_session.hSharedSecret, 0);

		BCryptDestroyKey(hPartnerPublicKey);

		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
			return false;
		}

		// Derive raw secret bytes
		DWORD secretSize = 0;
		status = BCryptDeriveKey(g_session.hSharedSecret, BCRYPT_KDF_HASH, nullptr,
			nullptr, 0, &secretSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to get shared secret size\n");
			return false;
		}

		g_session.sharedSecretData.resize(secretSize);
		status = BCryptDeriveKey(g_session.hSharedSecret, BCRYPT_KDF_HASH, nullptr,
			g_session.sharedSecretData.data(), secretSize, &secretSize, 0);
		if (!BCRYPT_SUCCESS(status))
		{
			OutputDebugStringW(L"[Discrypt] Failed to derive shared secret data\n");
			return false;
		}

		OutputDebugStringW(L"[Discrypt] Shared secret derived successfully!\n");
		OutputDebugStringW((L"[Discrypt] Shared secret size: " + std::to_wstring(secretSize) + L" bytes\n").c_str());

		return true;
	}

	/// <summary>
	/// Get public key as hex string for display
	/// </summary>
	std::wstring GetPublicKeyHex()
	{
		if (g_session.publicKeyBlob.empty())
			return L"No public key generated";

		std::wstringstream ss;
		ss << std::hex << std::setfill(L'0');
		for (size_t i = 0; i < g_session.publicKeyBlob.size() && i < 32; ++i)
		{
			ss << std::setw(2) << g_session.publicKeyBlob[i];
			if (i < 31 && i < g_session.publicKeyBlob.size() - 1) ss << L":";
		}
		if (g_session.publicKeyBlob.size() > 32)
			ss << L"...";
		return ss.str();
	}

	/// <summary>
	/// Get shared secret as hex string for display
	/// </summary>
	std::wstring GetSharedSecretHex()
	{
		if (g_session.sharedSecretData.empty())
			return L"No shared secret";

		std::wstringstream ss;
		ss << std::hex << std::setfill(L'0');
		for (size_t i = 0; i < g_session.sharedSecretData.size() && i < 16; ++i)
		{
			ss << std::setw(2) << g_session.sharedSecretData[i];
			if (i < 15 && i < g_session.sharedSecretData.size() - 1) ss << L":";
		}
		if (g_session.sharedSecretData.size() > 16)
			ss << L"...";
		return ss.str();
	}

	struct FindDiscordData
	{
		HWND hwnd;
	};

	BOOL CALLBACK FindDiscordProc(HWND hwnd, LPARAM lParam)
	{
		// We only care about visible windows
		if (!IsWindowVisible(hwnd)) return TRUE;

		int length = GetWindowTextLengthW(hwnd);
		if (length == 0) return TRUE;

		std::wstring title(length, L'\0');
		GetWindowTextW(hwnd, title.data(), length + 1);

		// Check if the title contains "Discord"
		if (title.find(L"Discord") != std::wstring::npos)
		{
			auto* data = reinterpret_cast<FindDiscordData*>(lParam);
			data->hwnd = hwnd;
			return FALSE; // Stop enumerating, we found it!
		}
		return TRUE;
	}

	HWND FindDiscordWindowByTitle()
	{
		FindDiscordData data = { nullptr };
		EnumWindows(FindDiscordProc, reinterpret_cast<LPARAM>(&data));
		return data.hwnd;
	}

	/// <summary>
	/// Gets the Discord window handle, using cache if valid
	/// </summary>
	HWND GetDiscordWindow()
	{
		// Check if cached handle is still valid
		if (Discord != nullptr && IsWindow(Discord))
		{
			return Discord;
		}

		// Cache is invalid, search for Discord window
		Discord = FindDiscordWindowByTitle();

		if (Discord != nullptr)
		{
			OutputDebugStringW(L"[Discrypt] Discord window found and cached.\n");
		}

		return Discord;
	}

	/// <summary>
	/// Reads the current text from the Discord input box
	/// </summary>
	/// <returns>The current text in the Discord input box, or empty string if failed</returns>
	std::wstring ReadDiscordTextBox()
	{
		using namespace ::Microsoft::WRL;
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadDiscordTextBox: Failed to find Discord window.\n");
			CoUninitialize();
			return L"";
		}

		ComPtr<IUIAutomationElement> TextBoxElement;
		automation->ElementFromHandle(hwnd, &TextBoxElement);

		// Create conditions for Edit and Document controls
		ComPtr<IUIAutomationCondition> editCondition;
		VARIANT varPropEdit;
		varPropEdit.vt = VT_I4;
		varPropEdit.lVal = UIA_EditControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropEdit, &editCondition);

		ComPtr<IUIAutomationCondition> docCondition;
		VARIANT varPropDoc;
		varPropDoc.vt = VT_I4;
		varPropDoc.lVal = UIA_DocumentControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropDoc, &docCondition);

		ComPtr<IUIAutomationCondition> combinedCondition;
		automation->CreateOrCondition(editCondition.Get(), docCondition.Get(), &combinedCondition);

		// Find all text boxes
		ComPtr<IUIAutomationElementArray> TextBoxArray;
		TextBoxElement->FindAll(TreeScope_Subtree, combinedCondition.Get(), &TextBoxArray);

		if (TextBoxArray == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadDiscordTextBox: No text boxes found.\n");
			CoUninitialize();
			return L"";
		}

		int length = 0;
		TextBoxArray->get_Length(&length);

		if (length == 0)
		{
			OutputDebugStringW(L"[Discrypt] ReadDiscordTextBox: Text box array is empty.\n");
			CoUninitialize();
			return L"";
		}

		// Get the LAST element (the input box)
		ComPtr<IUIAutomationElement> lastTextBox;
		TextBoxArray->GetElement(length - 1, &lastTextBox);

		if (lastTextBox == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadDiscordTextBox: Failed to get last text box.\n");
			CoUninitialize();
			return L"";
		}

		// Get the current value using ValuePattern
		ComPtr<IUIAutomationValuePattern> valuePattern;
		HRESULT hrValue = lastTextBox->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern));

		std::wstring result = L"";
		if (SUCCEEDED(hrValue) && valuePattern != nullptr)
		{
			BSTR value = nullptr;
			valuePattern->get_CurrentValue(&value);

			if (value != nullptr)
			{
				result = value;
				SysFreeString(value);
			}

			OutputDebugStringW((L"[Discrypt] ReadDiscordTextBox: " + result + L"\n").c_str());
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] ReadDiscordTextBox: Failed to get ValuePattern.\n");
		}

		CoUninitialize();
		return result;
	}

	/// <summary>
	/// Simulates a key press using SendInput
	/// </summary>
	void SimulateKeyPress(WORD vkCode, bool isExtended = false)
	{
		INPUT inputs[2] = {};

		// Key down
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = vkCode;
		inputs[0].ki.dwFlags = isExtended ? KEYEVENTF_EXTENDEDKEY : 0;

		// Key up
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = vkCode;
		inputs[1].ki.dwFlags = (isExtended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;

		SendInput(2, inputs, sizeof(INPUT));
	}

	/// <summary>
	/// Simulates typing a string character by character
	/// </summary>
	void SimulateTyping(const std::wstring& text)
	{
		for (wchar_t ch : text)
		{
			INPUT inputs[2] = {};

			// Key down
			inputs[0].type = INPUT_KEYBOARD;
			inputs[0].ki.wScan = ch;
			inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

			// Key up
			inputs[1].type = INPUT_KEYBOARD;
			inputs[1].ki.wScan = ch;
			inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

			SendInput(2, inputs, sizeof(INPUT));
			// No delay - maximum speed!
		}
	}

	/// <summary>
	/// Writes text to the Discord input box
	/// </summary>
	/// <param name="text">The text to write to the Discord input box</param>
	/// <returns>True if successful, false otherwise</returns>
	bool WriteDiscordTextBox(const std::wstring& text)
	{
		using namespace ::Microsoft::WRL;
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteDiscordTextBox: Failed to find Discord window.\n");
			CoUninitialize();
			return false;
		}

		ComPtr<IUIAutomationElement> TextBoxElement;
		automation->ElementFromHandle(hwnd, &TextBoxElement);

		// Create conditions for Edit and Document controls
		ComPtr<IUIAutomationCondition> editCondition;
		VARIANT varPropEdit;
		varPropEdit.vt = VT_I4;
		varPropEdit.lVal = UIA_EditControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropEdit, &editCondition);

		ComPtr<IUIAutomationCondition> docCondition;
		VARIANT varPropDoc;
		varPropDoc.vt = VT_I4;
		varPropDoc.lVal = UIA_DocumentControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropDoc, &docCondition);

		ComPtr<IUIAutomationCondition> combinedCondition;
		automation->CreateOrCondition(editCondition.Get(), docCondition.Get(), &combinedCondition);

		// Find all text boxes
		ComPtr<IUIAutomationElementArray> TextBoxArray;
		TextBoxElement->FindAll(TreeScope_Subtree, combinedCondition.Get(), &TextBoxArray);

		if (TextBoxArray == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteDiscordTextBox: No text boxes found.\n");
			CoUninitialize();
			return false;
		}

		int length = 0;
		TextBoxArray->get_Length(&length);

		if (length == 0)
		{
			OutputDebugStringW(L"[Discrypt] WriteDiscordTextBox: Text box array is empty.\n");
			CoUninitialize();
			return false;
		}

		// Get the LAST element (the input box)
		ComPtr<IUIAutomationElement> lastTextBox;
		TextBoxArray->GetElement(length - 1, &lastTextBox);

		if (lastTextBox == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteDiscordTextBox: Failed to get last text box.\n");
			CoUninitialize();
			return false;
		}

		// Set focus to the text box first
		HRESULT hrFocus = lastTextBox->SetFocus();
		if (FAILED(hrFocus))
		{
			OutputDebugStringW(L"[Discrypt] WriteDiscordTextBox: Failed to focus text box.\n");
		}

		Sleep(20); // Reduced delay - just enough for focus

		// Select all existing text with Ctrl+A
		INPUT inputs[4] = {};

		// Ctrl down
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = VK_CONTROL;

		// A down
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = 'A';

		// A up
		inputs[2].type = INPUT_KEYBOARD;
		inputs[2].ki.wVk = 'A';
		inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

		// Ctrl up
		inputs[3].type = INPUT_KEYBOARD;
		inputs[3].ki.wVk = VK_CONTROL;
		inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

		SendInput(4, inputs, sizeof(INPUT));
		Sleep(20); // Reduced delay

		// Type the new text using Unicode input
		SimulateTyping(text);

		OutputDebugStringW((L"[Discrypt] WriteDiscordTextBox: Successfully simulated typing: " + text + L"\n").c_str());

		CoUninitialize();
		return true;
	}


	/// <summary>
	/// Created a keyboard hook to intercept key presses globally
	/// </summary>

	LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode == HC_ACTION)
		{
			KBDLLHOOKSTRUCT* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
			if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
			{
				// Check if Enter key is pressed
				if (pKeyboard->vkCode == VK_RETURN)
				{
					// Check if Alt key is also held down (Alt+Enter combination)
					bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

					if (altPressed)
					{
						// Skip if this is an injected keypress (our own simulated Enter)
						if (pKeyboard->flags & LLKHF_INJECTED)
						{
							OutputDebugStringW(L"[Discrypt] Ignoring injected Enter key.\n");
							return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
						}

						// Check if Discord window is active
						HWND foregroundWindow = GetForegroundWindow();
						HWND discordWindow = GetDiscordWindow();

						if (foregroundWindow == discordWindow && discordWindow != nullptr)
						{
							OutputDebugStringW(L"[Discrypt] Alt+Enter pressed in Discord! Processing...\n");

							// Read current Discord text
							std::wstring originalText = ReadDiscordTextBox();

							std::wstring modifiedText;

							// Check if this is a handshake initiation or response
							if (originalText.find(L"HANDSHAKE_INIT:") == 0)
							{
								// Partner initiated handshake - respond with our public key
								OutputDebugStringW(L"[Discrypt] Responding to handshake initiation...\n");

								// Extract partner's public key
								size_t colonPos = originalText.find(L':');
								if (colonPos != std::wstring::npos)
								{
									std::wstring partnerKeyBase64 = originalText.substr(colonPos + 1);
									g_session.partnerPublicKeyBlob = Base64Decode(partnerKeyBase64);

									OutputDebugStringW((L"[Discrypt] Received partner public key (" +
										std::to_wstring(g_session.partnerPublicKeyBlob.size()) + L" bytes)\n").c_str());
								}

								// Generate our key pair if we haven't already
								if (!g_session.hPrivateKey)
								{
									if (!GenerateDHKeyPair())
									{
										OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
										return 1;
									}
								}

								// Derive shared secret
								if (!DeriveSharedSecret(g_session.partnerPublicKeyBlob))
								{
									OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
									return 1;
								}

								// Send our public key as response
								std::wstring ourPublicKeyBase64 = Base64Encode(g_session.publicKeyBlob);
								modifiedText = L"HANDSHAKE_RESPONSE:" + ourPublicKeyBase64;

								g_session.state = EncryptionSession::State::HandshakeComplete;
								OutputDebugStringW(L"[Discrypt] Handshake complete (responder)!\n");
								OutputDebugStringW((L"[Discrypt] Shared Secret: " + GetSharedSecretHex() + L"\n").c_str());
							}
							else if (originalText.find(L"HANDSHAKE_RESPONSE:") == 0)
							{
								// Received response to our handshake initiation
								OutputDebugStringW(L"[Discrypt] Received handshake response...\n");

								// Extract partner's public key
								size_t colonPos = originalText.find(L':');
								if (colonPos != std::wstring::npos)
								{
									std::wstring partnerKeyBase64 = originalText.substr(colonPos + 1);
									g_session.partnerPublicKeyBlob = Base64Decode(partnerKeyBase64);

									OutputDebugStringW((L"[Discrypt] Received partner public key (" +
										std::to_wstring(g_session.partnerPublicKeyBlob.size()) + L" bytes)\n").c_str());
								}

								// Derive shared secret
								if (!DeriveSharedSecret(g_session.partnerPublicKeyBlob))
								{
									OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
									return 1;
								}

								g_session.state = EncryptionSession::State::HandshakeComplete;
								OutputDebugStringW(L"[Discrypt] Handshake complete (initiator)!\n");
								OutputDebugStringW((L"[Discrypt] Shared Secret: " + GetSharedSecretHex() + L"\n").c_str());

								// Don't send anything - just complete the handshake
								return 1; // Block the key
							}
							else if (g_session.state == EncryptionSession::State::NoHandshake)
							{
								// No handshake yet - initiate it
								OutputDebugStringW(L"[Discrypt] Initiating handshake...\n");

								if (!GenerateDHKeyPair())
								{
									OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
									return 1;
								}

								std::wstring ourPublicKeyBase64 = Base64Encode(g_session.publicKeyBlob);
								modifiedText = L"HANDSHAKE_INIT:" + ourPublicKeyBase64;

								g_session.state = EncryptionSession::State::HandshakeInitiated;
								OutputDebugStringW((L"[Discrypt] Public Key: " + GetPublicKeyHex() + L"\n").c_str());
							}
							else if (g_session.state == EncryptionSession::State::HandshakeComplete)
							{
								// Handshake complete - in future this will encrypt the message
								OutputDebugStringW(L"[Discrypt] Handshake already complete - ready for encryption\n");
								OutputDebugStringW((L"[Discrypt] Shared Secret: " + GetSharedSecretHex() + L"\n").c_str());

								// For now, just prefix with a marker
								modifiedText = L"[ENCRYPTED] " + originalText;
							}
							else
							{
								// Handshake initiated but not complete
								modifiedText = originalText;
								OutputDebugStringW(L"[Discrypt] Waiting for handshake response...\n");
							}

							OutputDebugStringW((L"[Discrypt] Original: " + originalText + L"\n").c_str());
							OutputDebugStringW((L"[Discrypt] Modified: " + modifiedText + L"\n").c_str());

							// Write the modified text back
							WriteDiscordTextBox(modifiedText);

							// Reduced delay - just enough for text to be written
							Sleep(50);

							// Now send Enter programmatically to send the message
							OutputDebugStringW(L"[Discrypt] Sending Enter key programmatically...\n");
							SimulateKeyPress(VK_RETURN);

							// BLOCK the Alt+Enter from reaching Discord
							return 1;
						}
					}
				}

				OutputDebugStringW((L"[Discrypt] Key Pressed: " + std::to_wstring(pKeyboard->vkCode) + L"\n").c_str());
			}
		}
		return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
	}


	/// <summary>
	/// Initializes the singleton application object.  This is the first line of authored code
	/// executed, and as such is the logical equivalent of main() or WinMain().
	/// </summary>
	App::App()
	{
		// Xaml objects should not call InitializeComponent during construction.
		// See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
		UnhandledException([](IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
			{
				if (IsDebuggerPresent())
				{
					auto errorMessage = e.Message();
					__debugbreak();
				}
			});
#endif
	}

	/// <summary>
	/// Invoked when the application is launched.
	/// </summary>
	/// <param name="e">Details about the launch request and process.</param>
	void App::OnLaunched([[maybe_unused]] Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e)
	{
		window = make<MainWindow>();
		window.Activate();

		// Install the keyboard hook to monitor Enter key in Discord
		keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);

		if (keyboardHook != nullptr)
		{
			OutputDebugStringW(L"[Discrypt] Keyboard hook installed successfully.\n");
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] Failed to install keyboard hook.\n");
		}
	}

	App::~App()
	{
		if (keyboardHook)
		{
			UnhookWindowsHookEx(keyboardHook);
			keyboardHook = nullptr;
		}

		// Cleanup BCrypt resources
		if (g_session.hPrivateKey)
		{
			BCryptDestroyKey(g_session.hPrivateKey);
			g_session.hPrivateKey = nullptr;
		}
		if (g_session.hSharedSecret)
		{
			BCryptDestroySecret(g_session.hSharedSecret);
			g_session.hSharedSecret = nullptr;
		}
		if (g_session.hAlgorithm)
		{
			BCryptCloseAlgorithmProvider(g_session.hAlgorithm, 0);
			g_session.hAlgorithm = nullptr;
		}
	}
	

}
