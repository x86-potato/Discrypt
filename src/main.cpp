
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")



#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h> // For SHGetKnownFolderPath
#include <thread>
#include <iostream>
#include <filesystem>


#include "CryptoManager.h"
#include "DatabaseManager.h"
#include "DiscordIPCModule.h"
#include "JSInjection.h"

#include "../resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_ACTION1 1002
#define ID_TRAY_ACTION2 1003

std::wstring g_encryptionKey = L""; // Global storage for the password/key
Discrypt::EncryptionSession g_session;
std::wstring g_userHandle = L""; // User's @handle for identification
std::wstring g_partnerHandle = L""; // Current conversation partner's @handle

::Discrypt::DatabaseManager g_database; // Global database instance
Discrypt::DiscordIpcModule g_IPCModule; // Global IPC module instance
HINSTANCE g_hInst;
NOTIFYICONDATA nid;
HWND g_hwnd;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

std::wstring GetDatabasePath() {
    wchar_t* localAppData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData);

    if (SUCCEEDED(hr)) {
        std::wstring path = localAppData;
        CoTaskMemFree(localAppData);

        path += L"\\Discrypt32";
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }

        path += L"\\vault.db";
        return path;
    }
    return L"vault.db"; // Fallback
}

void AddTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    // Load the custom icon from your resources
    nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON1));

    lstrcpy(nid.szTip, TEXT("Discrypt32"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}
void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();

    // Using MIIM_STRING and proper flags makes the text rendering sharper
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ACTION1, L"Inject into Discord");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ACTION2, L"Reset keys");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    // Fix: Foreground window trick prevents the menu from staying open when clicking away
    SetForegroundWindow(hwnd);

    // TrackPopupMenuEx with TPM_BOTTOMALIGN looks more modern in the tray area
    TrackPopupMenuEx(hMenu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, hwnd, NULL);

    DestroyMenu(hMenu);
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    SetProcessDPIAware();
    // --- 1. ALLOCATE CONSOLE FOR STD::COUT LOGGING ---
#ifdef _DEBUG
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    std::wcout << L"[Discrypt] Debug Console Initialized.\n"; // CHANGED
#endif
    g_hInst = hInstance;

    // --- 2. INITIALIZE DATABASE ---
    std::wstring dbPath = GetDatabasePath();
    if (!g_database.Initialize(dbPath)) {
        MessageBox(NULL, TEXT("Failed to initialize local database!"), TEXT("Error"), MB_ICONERROR);
        return -1;
    }

    // --- 3. CREATE WINDOW & TRAY ICON ---
    const TCHAR CLASS_NAME[] = TEXT("TrayWindowClass");
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        0, CLASS_NAME, TEXT("App"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        NULL, NULL, hInstance, NULL
    );

    AddTrayIcon(g_hwnd);

    // --- 4. START MODULES ---
    g_IPCModule.start();

    std::thread([]() {
        ::Discrypt::DiscordInjector jsInjector;
        std::wcout << L"[Discrypt] Starting Discord injection...\n"; // CHANGED

        bool success = jsInjector.Inject();

        if (success) {
            std::wcout << L"[Discrypt] Discord injection completed successfully\n"; // CHANGED
        }
        else {
            std::wcout << L"[Discrypt] ERROR: Discord injection failed\n"; // CHANGED
        }
	}).detach();    // --- 5. RUN MESSAGE LOOP ---

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RemoveTrayIcon();
#ifdef _DEBUG
    FreeConsole();
#endif

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_ACTION1:
            // INJECT INTO DISCORD BUTTON
            std::wcout << L"[Tray] Manual injection requested...\n"; // CHANGED
            std::thread([]() {
                ::Discrypt::DiscordInjector jsInjector;
                bool success = jsInjector.Inject();
                if (success) {
                    std::wcout << L"[Tray] Manual injection successful.\n"; // CHANGED
                }
                else {
                    std::wcout << L"[Tray] Manual injection failed.\n"; // CHANGED
                }
                }).detach();
            break;

        case ID_TRAY_ACTION2:
            // RESET KEYS BUTTON
        {
            int response = MessageBox(
                hwnd,
                TEXT("Are you sure you want to reset all encryption keys?\n\nThis will break existing secure chats until a new handshake is performed with each user."),
                TEXT("Confirm Reset"),
                MB_YESNO | MB_ICONWARNING
            );

            if (response == IDYES) {
                if (g_database.ClearAllSessions()) {
                    std::wcout << L"[Tray] Database cleared successfully.\n"; // CHANGED
                    MessageBox(hwnd, TEXT("All keys have been reset."), TEXT("Success"), MB_OK | MB_ICONINFORMATION);
                }
                else {
                    std::wcout << L"[Tray] ERROR: Failed to clear database.\n"; // CHANGED
                    MessageBox(hwnd, TEXT("Failed to clear encryption keys from the database."), TEXT("Error"), MB_OK | MB_ICONERROR);
                }
            }
        }
        break;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}