#pragma once

#include "MainWindow.g.h"
#include <map>
#include <vector>

namespace winrt::Discrypt::implementation
{
    struct MessageData
    {
        std::wstring timestamp;
        std::wstring content;
        bool isDecrypted;
        bool isSent; // true if this is a message we sent
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow()
        {
            m_currentUser = L"";
        }

        // Event handlers
        void NavView_Loaded(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void NavView_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
                                      winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        void DecryptButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ClearButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ClearAllMessages_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // Helper methods
        void UpdateMessageHistory();
        void AddMessageToHistory(const std::wstring& user, const MessageData& message);
        void SaveMessagesToFile(const std::wstring& user);
        void LoadMessagesFromFile(const std::wstring& user);
        void UpdateHandshakeStatus();
        void UpdateHomeView();
		void AddSentMessageToHistory(const std::wstring& senderHandle, const std::wstring& message);
		void AddReceivedMessageToHistory(const std::wstring& senderHandle, const std::wstring& message);
		void AddUserButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void AddConversation(winrt::hstring const& handle);
		void UserHandleInput_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);

    private:
        std::wstring m_currentUser;
        std::map<std::wstring, std::vector<MessageData>> m_userMessages;
    };
}

namespace winrt::Discrypt::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
