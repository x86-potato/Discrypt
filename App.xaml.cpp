#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "CryptoManager.h"
#include "DiscordInterop.h"
#include "KeyboardHook.h"

#pragma comment(lib, "bcrypt.lib")

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

// Global state
std::wstring g_encryptionKey = L""; // Global storage for the password/key
Discrypt::EncryptionSession g_session;

namespace winrt::Discrypt::implementation
{
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

			// Extract partner's public key
			size_t colonPos = originalText.find(L':');
			if (colonPos != std::wstring::npos)
			{
				std::wstring partnerKeyBase64 = originalText.substr(colonPos + 1);
				g_session.partnerPublicKeyBlob = ::Discrypt::CryptoManager::Base64Decode(partnerKeyBase64);

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

			// Send our public key as response
			std::wstring ourPublicKeyBase64 = ::Discrypt::CryptoManager::Base64Encode(g_session.publicKeyBlob);
			modifiedText = L"HANDSHAKE_RESPONSE:" + ourPublicKeyBase64;

			g_session.state = ::Discrypt::EncryptionSession::State::HandshakeComplete;
			OutputDebugStringW(L"[Discrypt] Handshake complete (responder)!\n");
			OutputDebugStringW((L"[Discrypt] Shared Secret: " + ::Discrypt::CryptoManager::GetSharedSecretHex(g_session) + L"\n").c_str());
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
				g_session.partnerPublicKeyBlob = ::Discrypt::CryptoManager::Base64Decode(partnerKeyBase64);

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

			std::wstring ourPublicKeyBase64 = ::Discrypt::CryptoManager::Base64Encode(g_session.publicKeyBlob);
			modifiedText = L"HANDSHAKE_INIT:" + ourPublicKeyBase64;

			g_session.state = ::Discrypt::EncryptionSession::State::HandshakeInitiated;
			OutputDebugStringW((L"[Discrypt] Public Key: " + ::Discrypt::CryptoManager::GetPublicKeyHex(g_session) + L"\n").c_str());
		}
		else if (originalText.find(L"[ENC]:") == 0)
		{
			// Encrypted message detected - decrypt it
			OutputDebugStringW(L"[Discrypt] Encrypted message detected, decrypting...\n");

			if (g_session.state != ::Discrypt::EncryptionSession::State::HandshakeComplete)
			{
				OutputDebugStringW(L"[Discrypt] Cannot decrypt - no handshake completed\n");
				return;
			}

			std::wstring decrypted = ::Discrypt::CryptoManager::DecryptMessage(g_session, originalText);

			// Replace encrypted text with decrypted text for display
			::Discrypt::DiscordInterop::WriteTextBox(decrypted);
			OutputDebugStringW((L"[Discrypt] Decrypted: " + decrypted + L"\n").c_str());

			// Don't send anything - just show the decrypted message
			return;
		}
		else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
		{
			// Handshake complete - encrypt the message
			OutputDebugStringW(L"[Discrypt] Encrypting message...\n");
			OutputDebugStringW((L"[Discrypt] Shared Secret: " + ::Discrypt::CryptoManager::GetSharedSecretHex(g_session) + L"\n").c_str());

			if (originalText.empty())
			{
				OutputDebugStringW(L"[Discrypt] Empty message, nothing to encrypt\n");
				return;
			}

			// Encrypt the message
			modifiedText = ::Discrypt::CryptoManager::EncryptMessage(g_session, originalText);

			if (modifiedText.empty())
			{
				OutputDebugStringW(L"[Discrypt] Encryption failed\n");
				return;
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

		// Write the modified text back
		::Discrypt::DiscordInterop::WriteTextBox(modifiedText);

		// Reduced delay - just enough for text to be written
		Sleep(50);

		// Now send Enter programmatically to send the message
		OutputDebugStringW(L"[Discrypt] Sending Enter key programmatically...\n");
		::Discrypt::KeyboardHook::SimulateKeyPress(VK_RETURN);
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

		// Set default window size
		auto appWindow = window.AppWindow();
		if (appWindow)
		{
			appWindow.Resize({ 480, 600 });
		}

		window.Activate();

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
