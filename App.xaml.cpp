#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "CryptoManager.h"
#include "DatabaseManager.h"
#include "DiscordIPCModule.h"
#include "JSInjection.h"
#include <chrono>
#include <ctime>
#include <fstream>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Shell32.lib")

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

// Global state
std::wstring g_encryptionKey = L""; // Global storage for the password/key
Discrypt::EncryptionSession g_session;
winrt::Discrypt::implementation::MainWindow* g_mainWindow = nullptr;
std::wstring g_userHandle = L""; // User's @handle for identification
std::wstring g_partnerHandle = L""; // Current conversation partner's @handle
::Discrypt::DatabaseManager g_database; // Global database instance
Discrypt::DiscordIpcModule g_ipcModule; // Global IPC module instance


namespace winrt::Discrypt::implementation
{
	/// <summary>
	/// Load user handle from persistent storage (database)
	/// </summary>
	std::wstring LoadUserHandle()
	{
		std::wstring handle = g_database.LoadUserHandle();
		if (!handle.empty())
		{
			OutputDebugStringW((L"[Discrypt] Loaded user handle from database: " + handle + L"\n").c_str());
		}
		return handle;
	}

	/// <summary>
	/// Save user handle to persistent storage (database)
	/// </summary>
	void SaveUserHandle(const std::wstring& handle)
	{
		if (g_database.SaveUserHandle(handle))
		{
			OutputDebugStringW((L"[Discrypt] Saved user handle to database: " + handle + L"\n").c_str());
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] Failed to save user handle to database\n");
		}
	}

	// NOTE: PromptForUserHandle() function has been removed.
	// Username is now entered and persisted via the homepage UI (UserHandleInput TextBox).
	// The problematic ContentDialog approach caused COM circular reference issues.

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
		// Initialize database
		WCHAR localAppDataPath[MAX_PATH];
		if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppDataPath) == S_OK)
		{
			OutputDebugStringW(L"[Discrypt] LocalAppData path: ");
			OutputDebugStringW(localAppDataPath);
			OutputDebugStringW(L"\n");

			std::wstring discryptDir = std::wstring(localAppDataPath) + L"\\Discrypt";
			std::wstring dbPath = discryptDir + L"\\discrypt.db";

			// Try to create directory and check for errors
			if (!CreateDirectoryW(discryptDir.c_str(), NULL))
			{
				DWORD error = GetLastError();
				if (error != ERROR_ALREADY_EXISTS)
				{
					OutputDebugStringW(L"[Discrypt] Failed to create directory. Error code: ");
					OutputDebugStringW(std::to_wstring(error).c_str());
					OutputDebugStringW(L"\n");
				}
				else
				{
					OutputDebugStringW(L"[Discrypt] Directory already exists\n");
				}
			}
			else
			{
				OutputDebugStringW(L"[Discrypt] Directory created successfully\n");
			}

			OutputDebugStringW(L"[Discrypt] Attempting to create database at: ");
			OutputDebugStringW(dbPath.c_str());
			OutputDebugStringW(L"\n");

			if (g_database.Initialize(dbPath))
			{
				OutputDebugStringW(L"[Discrypt] Database initialized successfully\n");
			}
			else
			{
				OutputDebugStringW(L"[Discrypt] Failed to initialize database\n");
			}
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] Failed to get LocalAppData path\n");
		}

		auto mainWindowImpl = make_self<MainWindow>();
		g_mainWindow = mainWindowImpl.get();
		window = *mainWindowImpl;

		// Set compact window size for minimal control panel
		auto appWindow = window.AppWindow();
		if (appWindow)
		{
			appWindow.Resize({ 450, 600 });
		}

		window.Activate();

		// Load user handle from database (used for crypto handshake)
		g_userHandle = LoadUserHandle();
		if (!g_userHandle.empty())
		{
			OutputDebugStringW((L"[Discrypt] Using stored handle: " + g_userHandle + L"\n").c_str());
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] No stored handle found. Will use default or auto-generated handle.\n");
			// Auto-generate a handle if needed
			g_userHandle = L"@user" + std::to_wstring(GetTickCount64() % 10000);
			SaveUserHandle(g_userHandle);
			OutputDebugStringW((L"[Discrypt] Auto-generated handle: " + g_userHandle + L"\n").c_str());
		}


		// Start IPC WebSocket server
		OutputDebugStringW(L"[Discrypt] Starting IPC WebSocket server...\n");
		if (g_ipcModule.start())
		{
			OutputDebugStringW(L"[Discrypt] IPC WebSocket server started successfully\n");
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] ERROR: Failed to start IPC WebSocket server\n");
		}

		// Begin injection
		std::thread([mainWindowImpl]() {

			::Discrypt::DiscordInjector jsInjector;
			OutputDebugStringW(L"[Discrypt] Starting Discord injection...\n");
			bool success = jsInjector.Inject();

			if (mainWindowImpl)
			{
				mainWindowImpl->DispatcherQueue().TryEnqueue([mainWindowImpl, success]()
				{
					mainWindowImpl->UpdateInjectionStatus(success);
				});
			}

			if (success)
			{
				OutputDebugStringW(L"[Discrypt] Discord injection completed successfully\n");
			}
			else
			{
				OutputDebugStringW(L"[Discrypt] ERROR: Discord injection failed\n");
			}
			}).detach();
	}

	App::~App()
	{

		// Cleanup encryption session
		::Discrypt::CryptoManager::CleanupSession(g_session);

		g_ipcModule.stop();
	}
}
