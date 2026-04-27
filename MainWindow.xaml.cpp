#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

// Extern declaration to access the global encryption key from App.xaml.cpp
extern std::wstring g_encryptionKey;

// Forward declarations for functions in App.xaml.cpp
namespace winrt::Discrypt::implementation
{
    std::wstring GetPublicKeyHex();
    std::wstring GetSharedSecretHex();
}

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
        std::wstring publicKey = GetPublicKeyHex();
        PublicKeyDisplay().Text(publicKey);

        // Update shared secret display
        std::wstring sharedSecret = GetSharedSecretHex();
        SharedSecretDisplay().Text(sharedSecret);

        OutputDebugStringW(L"[Discrypt] Keys refreshed in UI\n");
    }
}
