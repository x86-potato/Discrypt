#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "CryptoManager.h"
#include "App.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

// Extern declarations
extern std::wstring g_encryptionKey;
extern ::Discrypt::EncryptionSession g_session;

namespace winrt::Discrypt::implementation
{
    // This gets called when the toggle switch is flipped
    void MainWindow::StatusSwitch_Toggled(IInspectable const& sender, RoutedEventArgs const& e)
    {
        auto toggle = sender.as<Controls::ToggleSwitch>();
        bool isOn = toggle.IsOn();

        // This is like your console code handling user input!
        if (isOn)
        {
            // Enable encryption logic here
            OutputDebugStringW(L"Encryption ENABLED\n");
        }
        else
        {
            // Disable encryption logic here
            OutputDebugStringW(L"Encryption DISABLED\n");
        }
    }

    // This gets called when the password text changes
    void MainWindow::KeyInput_PasswordChanged(IInspectable const& sender, RoutedEventArgs const& e)
    {
        auto passwordBox = sender.as<Controls::PasswordBox>();
        hstring key = passwordBox.Password();

        // Update the global encryption key
        g_encryptionKey = key.c_str();

        // Access the key value
        OutputDebugStringW(L"Key changed: ");
        OutputDebugStringW(key.c_str());
        OutputDebugStringW(L"\n");
    }

    // This gets called when the refresh button is clicked
    void MainWindow::RefreshButton_Click(IInspectable const& sender, RoutedEventArgs const& e)
    {
        // Update public key display
        std::wstring publicKey = ::Discrypt::CryptoManager::GetPublicKeyHex(g_session);
        PublicKeyDisplay().Text(publicKey);

        // Update shared secret display
        std::wstring sharedSecret = ::Discrypt::CryptoManager::GetSharedSecretHex(g_session);
        SharedSecretDisplay().Text(sharedSecret);

        OutputDebugStringW(L"[Discrypt] Keys refreshed in UI\n");
    }

    void MainWindow::StartMonitorButton_Click(IInspectable const& sender, RoutedEventArgs const& e)
    {
        OutputDebugStringW(L"[Discrypt] Start Monitoring button clicked\n");

        // Get the App instance and start monitoring
        auto app = Application::Current().as<winrt::Discrypt::implementation::App>();
        app->StartMessageMonitoring();
    }

    void MainWindow::StopMonitorButton_Click(IInspectable const& sender, RoutedEventArgs const& e)
    {
        OutputDebugStringW(L"[Discrypt] Stop Monitoring button clicked\n");

        // Get the App instance and stop monitoring
        auto app = Application::Current().as<winrt::Discrypt::implementation::App>();
        app->StopMessageMonitoring();
    }
}
