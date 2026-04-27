#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "CryptoManager.h"
#include "DiscordInterop.h"
#include "KeyboardHook.h"
#include <chrono>
#include <ctime>
#include <fstream>

#pragma comment(lib, "bcrypt.lib")

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

// Global state
std::wstring g_encryptionKey = L""; // Global storage for the password/key
Discrypt::EncryptionSession g_session;
winrt::Discrypt::implementation::MainWindow* g_mainWindow = nullptr;
std::wstring g_userHandle = L""; // User's @handle for identification
std::wstring g_partnerHandle = L""; // Current conversation partner's @handle

namespace winrt::Discrypt::implementation
{
	/// <summary>
	/// Load user handle from persistent storage
	/// </summary>
	std::wstring LoadUserHandle()
	{
		WCHAR tempPath[MAX_PATH];
		GetTempPathW(MAX_PATH, tempPath);
		std::wstring folderPath = std::wstring(tempPath) + L"Discrypt\\";
		std::wstring filePath = folderPath + L"user_handle.txt";

		std::wifstream file(filePath);
		if (file.is_open())
		{
			std::wstring handle;
			if (std::getline(file, handle))
			{
				file.close();
				OutputDebugStringW((L"[Discrypt] Loaded user handle: " + handle + L"\n").c_str());
				return handle;
			}
			file.close();
		}
		return L"";
	}

	/// <summary>
	/// Save user handle to persistent storage
	/// </summary>
	void SaveUserHandle(const std::wstring& handle)
	{
		WCHAR tempPath[MAX_PATH];
		GetTempPathW(MAX_PATH, tempPath);
		std::wstring folderPath = std::wstring(tempPath) + L"Discrypt\\";
		CreateDirectoryW(folderPath.c_str(), NULL);

		std::wstring filePath = folderPath + L"user_handle.txt";
		std::wofstream file(filePath, std::ios::out | std::ios::trunc);
		if (file.is_open())
		{
			file << handle;
			file.close();
			OutputDebugStringW((L"[Discrypt] Saved user handle: " + handle + L"\n").c_str());
		}
	}

	// NOTE: PromptForUserHandle() function has been removed.
	// Username is now entered and persisted via the homepage UI (UserHandleInput TextBox).
	// The problematic ContentDialog approach caused COM circular reference issues.

	/// <summary>
	/// Handles Alt+Enter key press in Discord
	/// </summary>
	void HandleAltEnter()
	{
		// Read current Discord text
		std::wstring originalText = ::Discrypt::DiscordInterop::ReadTextBox();
		std::wstring modifiedText;

		// Check if this is a handshake initiation or response
		if (originalText.find(L"HANDSHAKE_INIT:") == 0)
		{
			// Partner initiated handshake - respond with our public key
			OutputDebugStringW(L"[Discrypt] Responding to handshake initiation...\n");

			// Extract partner's handle and public key
			// Format: HANDSHAKE_INIT:@handle:publickey
			size_t firstColon = originalText.find(L':');
			size_t secondColon = originalText.find(L':', firstColon + 1);
			std::wstring partnerHandle;
			if (firstColon != std::wstring::npos && secondColon != std::wstring::npos)
			{
				partnerHandle = originalText.substr(firstColon + 1, secondColon - firstColon - 1);
				g_partnerHandle = partnerHandle; // Store globally for message logging
				std::wstring partnerKeyBase64 = originalText.substr(secondColon + 1);
				g_session.partnerPublicKeyBlob = ::Discrypt::CryptoManager::Base64Decode(partnerKeyBase64);

				OutputDebugStringW((L"[Discrypt] Partner handle: " + partnerHandle + L"\n").c_str());

				OutputDebugStringW((L"[Discrypt] Received partner public key (" +
					std::to_wstring(g_session.partnerPublicKeyBlob.size()) + L" bytes)\n").c_str());
			}

			// Generate our key pair if we haven't already
			if (!g_session.hPrivateKey)
			{
				OutputDebugStringW(L"[Discrypt] No existing key pair, generating new one...\n");
				if (!::Discrypt::CryptoManager::GenerateDHKeyPair(g_session))
				{
					OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
					return;
				}
			}
			else
			{
				OutputDebugStringW(L"[Discrypt] Using existing key pair\n");
			}

			// Derive shared secret
			OutputDebugStringW(L"[Discrypt] About to derive shared secret (RESPONDER)...\n");
			bool derivationSuccess = ::Discrypt::CryptoManager::DeriveSharedSecret(g_session, g_session.partnerPublicKeyBlob);
			OutputDebugStringW((L"[Discrypt] Derivation result: " + std::to_wstring(derivationSuccess) + L"\n").c_str());
			OutputDebugStringW((L"[Discrypt] Shared secret data size after derivation: " + 
				std::to_wstring(g_session.sharedSecretData.size()) + L" bytes\n").c_str());

			if (!derivationSuccess)
			{
				OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
				return;
			}

			// Send our public key as response with our handle
			std::wstring ourPublicKeyBase64 = ::Discrypt::CryptoManager::Base64Encode(g_session.publicKeyBlob);
			modifiedText = L"HANDSHAKE_RESPONSE:" + g_userHandle + L":" + ourPublicKeyBase64;

			g_session.state = ::Discrypt::EncryptionSession::State::HandshakeComplete;
			OutputDebugStringW(L"[Discrypt] Handshake complete (responder)!\n");
			OutputDebugStringW((L"[Discrypt] Shared Secret: " + ::Discrypt::CryptoManager::GetSharedSecretHex(g_session) + L"\n").c_str());

			// Update UI
			if (g_mainWindow)
			{
				g_mainWindow->AddConversation(winrt::hstring(partnerHandle));
				g_mainWindow->UpdateHandshakeStatus();
			}
		}
		else if (originalText.find(L"HANDSHAKE_RESPONSE:") == 0)
		{
			// Received response to our handshake initiation
			OutputDebugStringW(L"[Discrypt] Received handshake response...\n");

			// Extract partner's handle and public key
			// Format: HANDSHAKE_RESPONSE:@handle:publickey
			size_t firstColon = originalText.find(L':');
			size_t secondColon = originalText.find(L':', firstColon + 1);

			std::wstring partnerHandle;
			if (firstColon != std::wstring::npos && secondColon != std::wstring::npos)
			{
				partnerHandle = originalText.substr(firstColon + 1, secondColon - firstColon - 1);
				g_partnerHandle = partnerHandle; // Store globally for message logging
				std::wstring partnerKeyBase64 = originalText.substr(secondColon + 1);
				g_session.partnerPublicKeyBlob = ::Discrypt::CryptoManager::Base64Decode(partnerKeyBase64);

				OutputDebugStringW((L"[Discrypt] Partner handle: " + partnerHandle + L"\n").c_str());

				OutputDebugStringW((L"[Discrypt] Received partner public key (" +
					std::to_wstring(g_session.partnerPublicKeyBlob.size()) + L" bytes)\n").c_str());
			}

			// Derive shared secret
			if (!::Discrypt::CryptoManager::DeriveSharedSecret(g_session, g_session.partnerPublicKeyBlob))
			{
				OutputDebugStringW(L"[Discrypt] Failed to derive shared secret\n");
				return;
			}

			g_session.state = ::Discrypt::EncryptionSession::State::HandshakeComplete;
			OutputDebugStringW(L"[Discrypt] Handshake complete (initiator)!\n");
			OutputDebugStringW((L"[Discrypt] Shared Secret: " + ::Discrypt::CryptoManager::GetSharedSecretHex(g_session) + L"\n").c_str());

			// Update UI
			if (g_mainWindow)
			{
				g_mainWindow->AddConversation(winrt::hstring(partnerHandle));
				g_mainWindow->UpdateHandshakeStatus();
			}

			// Don't send anything - just complete the handshake
			return;
		}
		else if (g_session.state == ::Discrypt::EncryptionSession::State::NoHandshake)
		{
			// No handshake yet - initiate it
			OutputDebugStringW(L"[Discrypt] Initiating handshake...\n");

			if (!::Discrypt::CryptoManager::GenerateDHKeyPair(g_session))
			{
				OutputDebugStringW(L"[Discrypt] Failed to generate key pair\n");
				return;
			}

			if (g_userHandle.empty())
			{

			}
			std::wstring ourPublicKeyBase64 = ::Discrypt::CryptoManager::Base64Encode(g_session.publicKeyBlob);
			modifiedText = L"HANDSHAKE_INIT:" + g_userHandle + L":" + ourPublicKeyBase64;

			g_session.state = ::Discrypt::EncryptionSession::State::HandshakeInitiated;
			OutputDebugStringW((L"[Discrypt] Public Key: " + ::Discrypt::CryptoManager::GetPublicKeyHex(g_session) + L"\n").c_str());

			// Update UI
			if (g_mainWindow)
			{
				g_mainWindow->UpdateHandshakeStatus();
			}
		}
		else if (originalText.find(L"[ENC]:") == 0)
		{
			// Encrypted message detected in Discord input - decrypt it in place
			OutputDebugStringW(L"[Discrypt] Encrypted message detected in Discord, decrypting in-place...\n");

			if (g_session.state != ::Discrypt::EncryptionSession::State::HandshakeComplete)
			{
				OutputDebugStringW(L"[Discrypt] Cannot decrypt - no handshake completed\n");
				return;
			}

			// Parse sender handle from message
			// Format: [ENC]:@sender:encrypteddata
			size_t firstColon = originalText.find(L':');
			size_t secondColon = originalText.find(L':', firstColon + 1);
			std::wstring senderHandle = L"unknown";
			std::wstring encryptedData = originalText;

			if (firstColon != std::wstring::npos && secondColon != std::wstring::npos)
			{
				senderHandle = originalText.substr(firstColon + 1, secondColon - firstColon - 1);
				encryptedData = L"[ENC]:" + originalText.substr(secondColon + 1); // Reconstruct for decryption
				OutputDebugStringW((L"[Discrypt] Sender: " + senderHandle + L"\n").c_str());
			}

			std::wstring decrypted = ::Discrypt::CryptoManager::DecryptMessage(g_session, encryptedData);

			// Replace encrypted text with decrypted text in Discord
			::Discrypt::DiscordInterop::WriteTextBox(decrypted);
			OutputDebugStringW((L"[Discrypt] Decrypted: " + decrypted + L"\n").c_str());

			// Add to app history for the sender
			if (g_mainWindow)
			{
				g_mainWindow->AddReceivedMessageToHistory(senderHandle, decrypted);
			}

			// Don't send anything - just show the decrypted message
			return;
		}
		else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
		{
			// Check if user handle is set before allowing encryption
			if (g_userHandle.empty())
			{
				OutputDebugStringW(L"[Discrypt] Cannot encrypt message - user handle not set!\n");
				OutputDebugStringW(L"[Discrypt] Please enter your Discord handle on the Home page first.\n");
				return;
			}

			// Handshake complete - encrypt the message
			OutputDebugStringW(L"[Discrypt] Encrypting message...\n");
			OutputDebugStringW((L"[Discrypt] Shared Secret: " + ::Discrypt::CryptoManager::GetSharedSecretHex(g_session) + L"\n").c_str());

			if (originalText.empty())
			{
				OutputDebugStringW(L"[Discrypt] Empty message, nothing to encrypt\n");
				return;
			}

			// Encrypt the message
			std::wstring encryptedData = ::Discrypt::CryptoManager::EncryptMessage(g_session, originalText);

			if (encryptedData.empty())
			{
				OutputDebugStringW(L"[Discrypt] Encryption failed\n");
				return;
			}

			// Add sender handle to encrypted message
			// Format: [ENC]:@sender:encrypteddata
			size_t colonPos = encryptedData.find(L':');
			if (colonPos != std::wstring::npos)
			{
				modifiedText = encryptedData.substr(0, colonPos + 1) + g_userHandle + L":" + encryptedData.substr(colonPos + 1);
			}
			else
			{
				modifiedText = encryptedData; // Fallback
			}

			// Add the original (decrypted) message to history
			if (g_mainWindow && !g_partnerHandle.empty())
			{
				// Log to partner's conversation, not our own handle
				g_mainWindow->AddSentMessageToHistory(g_partnerHandle, originalText);
			}
		}
		else
		{
			// Handshake initiated but not complete
			modifiedText = originalText;
			OutputDebugStringW(L"[Discrypt] Waiting for handshake response...\n");
		}

		OutputDebugStringW((L"[Discrypt] Original: " + originalText + L"\n").c_str());
		OutputDebugStringW((L"[Discrypt] Modified: " + modifiedText + L"\n").c_str());

		// Only write if we have modified text
		if (!modifiedText.empty())
		{
			// Write the modified text back
			::Discrypt::DiscordInterop::WriteTextBox(modifiedText);

			// Reduced delay - just enough for text to be written
			Sleep(50);

			// Now send Enter programmatically to send the message
			OutputDebugStringW(L"[Discrypt] Sending Enter key programmatically...\n");
			::Discrypt::KeyboardHook::SimulateKeyPress(VK_RETURN);
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] No modified text to send.\n");
		}
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
		auto mainWindowImpl = make_self<MainWindow>();
		g_mainWindow = mainWindowImpl.get();
		window = *mainWindowImpl;

		// Set default window size
		auto appWindow = window.AppWindow();
		if (appWindow)
		{
			appWindow.Resize({ 700, 1100 });
		}

		window.Activate();

		// Load or prompt for user handle
		g_userHandle = LoadUserHandle();
		if (!g_userHandle.empty())
		{
			OutputDebugStringW((L"[Discrypt] Using stored handle: " + g_userHandle + L"\n").c_str());
			// Populate the UserHandleInput on the home page
			if (mainWindowImpl)
			{
				mainWindowImpl->DispatcherQueue().TryEnqueue([mainWindowImpl]()
				{
					mainWindowImpl->UserHandleInput().Text(g_userHandle);
				});
			}
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] No stored handle found. Please enter your Discord handle on the Home page.\n");
		}

		// Install the keyboard hook with our callback
		::Discrypt::KeyboardHook::Install(HandleAltEnter);
	}

	App::~App()
	{
		// Uninstall keyboard hook
		::Discrypt::KeyboardHook::Uninstall();

		// Cleanup encryption session
		::Discrypt::CryptoManager::CleanupSession(g_session);
	}
}
