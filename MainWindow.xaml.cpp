#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "CryptoManager.h"
#include "App.xaml.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// Extern declarations
extern std::wstring g_encryptionKey;
extern ::Discrypt::EncryptionSession g_session;
extern std::wstring g_userHandle;

namespace winrt::Discrypt::implementation
{
    void MainWindow::NavView_Loaded(IInspectable const& sender, RoutedEventArgs const& e)
    {
        // Set Home as the default selected item
        NavView().SelectedItem(HomeMenuItem());
    }

    void MainWindow::NavView_SelectionChanged(NavigationView const& sender, NavigationViewSelectionChangedEventArgs const& args)
    {
        auto selectedItem = args.SelectedItem().try_as<NavigationViewItem>();
        if (selectedItem)
        {
            auto tag = unbox_value_or<hstring>(selectedItem.Tag(), L"");
            std::wstring tagStr = tag.c_str();

            // Show/hide appropriate view
            if (tagStr == L"home")
            {
                HomeView().Visibility(Visibility::Visible);
                ChatView().Visibility(Visibility::Collapsed);
                m_currentUser = L"";
                UpdateHomeView();
                OutputDebugStringW(L"[Discrypt] Switched to Home view\n");
            }
            else
            {
                HomeView().Visibility(Visibility::Collapsed);
                ChatView().Visibility(Visibility::Visible);
                m_currentUser = tagStr;

                // Update header
                CurrentUserDisplay().Text(L"Chat with: " + m_currentUser);

                // Update handshake status
                if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
                {
                    HandshakeStatusDisplay().Text(L"✓ Handshake complete");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 255, 0 }));
                }
                else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeInitiated)
                {
                    HandshakeStatusDisplay().Text(L"⏳ Handshake initiated");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 165, 0 }));
                }
                else
                {
                    HandshakeStatusDisplay().Text(L"⚠ No handshake - Use Alt+Enter in Discord to initiate");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 0, 0 }));
                }

                // Load messages for this user
                LoadMessagesFromFile(m_currentUser);
                UpdateMessageHistory();

                OutputDebugStringW((L"[Discrypt] Selected user: " + m_currentUser + L"\n").c_str());
            }
        }
    }

    void MainWindow::DecryptButton_Click(IInspectable const& sender, RoutedEventArgs const& e)
    {
        if (m_currentUser.empty() || m_currentUser == L"home")
        {
            // Show error - no user selected
            ContentDialog dialog;
            dialog.XamlRoot(this->Content().XamlRoot());
            dialog.Title(box_value(L"No User Selected"));
            dialog.Content(box_value(L"Please select a user from the menu first."));
            dialog.CloseButtonText(L"OK");
            dialog.ShowAsync();
            return;
        }

        std::wstring encryptedText = EncryptedMessageInput().Text().c_str();

        if (encryptedText.empty())
        {
            ContentDialog dialog;
            dialog.XamlRoot(this->Content().XamlRoot());
            dialog.Title(box_value(L"No Message"));
            dialog.Content(box_value(L"Please paste an encrypted message first."));
            dialog.CloseButtonText(L"OK");
            dialog.ShowAsync();
            return;
        }

        // Decrypt the message
        std::wstring decrypted = ::Discrypt::CryptoManager::DecryptMessage(g_session, encryptedText);

        // Check if decryption failed
        if (decrypted.find(L"[DECRYPT FAILED") != std::wstring::npos)
        {
            ContentDialog dialog;
            dialog.XamlRoot(this->Content().XamlRoot());
            dialog.Title(box_value(L"Decryption Failed"));
            dialog.Content(box_value(decrypted));
            dialog.CloseButtonText(L"OK");
            dialog.ShowAsync();
            return;
        }

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &time);

        std::wostringstream oss;
        oss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
        std::wstring timestamp = oss.str();

        // Create message data
        MessageData message;
        message.timestamp = timestamp;
        message.content = decrypted;
        message.isDecrypted = true;
        message.isSent = false; // This is a received message

        // Add to user's message history
        AddMessageToHistory(m_currentUser, message);

        // Save to file
        SaveMessagesToFile(m_currentUser);

        // Update display
        UpdateMessageHistory();

        // Clear input
        EncryptedMessageInput().Text(L"");

        OutputDebugStringW((L"[Discrypt] Decrypted and saved message for user: " + m_currentUser + L"\n").c_str());
    }

    void MainWindow::ClearButton_Click(IInspectable const& sender, RoutedEventArgs const& e)
    {
        EncryptedMessageInput().Text(L"");
    }

    void MainWindow::UpdateMessageHistory()
    {
        MessageHistoryPanel().Children().Clear();

        if (m_currentUser.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"Select a user from the menu");
            emptyText.Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 128, 128, 128 }));
            emptyText.TextAlignment(TextAlignment::Center);
            emptyText.Margin({ 0, 40, 0, 0 });
            MessageHistoryPanel().Children().Append(emptyText);
            return;
        }

        auto it = m_userMessages.find(m_currentUser);
        if (it == m_userMessages.end() || it->second.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"No messages yet. Paste encrypted messages below to decrypt.");
            emptyText.Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 128, 128, 128 }));
            emptyText.TextAlignment(TextAlignment::Center);
            emptyText.Margin({ 0, 40, 0, 0 });
            MessageHistoryPanel().Children().Append(emptyText);
            return;
        }

        // Display messages
        for (const auto& msg : it->second)
        {
            StackPanel messageCard;
            messageCard.Spacing(4);
            messageCard.Padding({ 12, 12, 12, 12 });

            // Different colors for sent vs received messages
            if (msg.isSent)
            {
                messageCard.Background(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 100, 0 })); // Dark green for sent
                messageCard.HorizontalAlignment(HorizontalAlignment::Right);
            }
            else
            {
                messageCard.Background(Media::SolidColorBrush(Windows::UI::Color{ 255, 32, 32, 32 })); // Dark gray for received
                messageCard.HorizontalAlignment(HorizontalAlignment::Left);
            }

            messageCard.CornerRadius({ 8, 8, 8, 8 });
            messageCard.MaxWidth(500);

            TextBlock timestampBlock;
            timestampBlock.Text(msg.isSent ? L"You • " + msg.timestamp : msg.timestamp);
            timestampBlock.FontSize(11);
            timestampBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 128, 128, 128 }));

            TextBlock contentBlock;
            contentBlock.Text(msg.content);
            contentBlock.FontSize(14);
            contentBlock.TextWrapping(TextWrapping::Wrap);
            contentBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 255, 255 }));

            messageCard.Children().Append(timestampBlock);
            messageCard.Children().Append(contentBlock);

            MessageHistoryPanel().Children().Append(messageCard);
        }

        // Auto-scroll to bottom after updating messages
        DispatcherQueue().TryEnqueue([this]()
            {
                // Find the ScrollViewer parent of MessageHistoryPanel
                auto parent = MessageHistoryPanel().Parent();
                if (auto scrollViewer = parent.try_as<Controls::ScrollViewer>())
                {
                    scrollViewer.ChangeView(nullptr, scrollViewer.ScrollableHeight(), nullptr);
                }
            });
    }

    void MainWindow::AddMessageToHistory(const std::wstring& user, const MessageData& message)
    {
        m_userMessages[user].push_back(message);
    }

    void MainWindow::SaveMessagesToFile(const std::wstring& user)
    {
        try
        {
            // Get app data folder
            auto localFolder = Windows::Storage::ApplicationData::Current().LocalFolder();
            std::wstring folderPath = localFolder.Path().c_str();

            // Create filename based on user
            std::wstring filename = folderPath + L"\\messages_" + user + L".txt";

            // Open file for writing
            std::wofstream file(filename, std::ios::out | std::ios::trunc);
            if (!file.is_open())
            {
                OutputDebugStringW(L"[Discrypt] Failed to open file for writing\n");
                return;
            }

            // Write messages
            auto it = m_userMessages.find(user);
            if (it != m_userMessages.end())
            {
                for (const auto& msg : it->second)
                {
                    file << msg.timestamp << L"|" << (msg.isSent ? L"SENT" : L"RECV") << L"|" << msg.content << L"\n";
                }
            }

            file.close();
            OutputDebugStringW((L"[Discrypt] Saved messages to: " + filename + L"\n").c_str());
        }
        catch (...)
        {
            OutputDebugStringW(L"[Discrypt] Exception saving messages\n");
        }
    }

    void MainWindow::LoadMessagesFromFile(const std::wstring& user)
    {
        try
        {
            // Get app data folder
            auto localFolder = Windows::Storage::ApplicationData::Current().LocalFolder();
            std::wstring folderPath = localFolder.Path().c_str();

            // Create filename based on user
            std::wstring filename = folderPath + L"\\messages_" + user + L".txt";

            // Open file for reading
            std::wifstream file(filename);
            if (!file.is_open())
            {
                OutputDebugStringW(L"[Discrypt] No existing message file found (this is normal for new users)\n");
                m_userMessages[user].clear();
                return;
            }

            // Clear existing messages for this user
            m_userMessages[user].clear();

            // Read messages
            std::wstring line;
            while (std::getline(file, line))
            {
                size_t separator1 = line.find(L'|');
                if (separator1 != std::wstring::npos)
                {
                    size_t separator2 = line.find(L'|', separator1 + 1);

                    MessageData msg;
                    msg.timestamp = line.substr(0, separator1);

                    if (separator2 != std::wstring::npos)
                    {
                        // New format with SENT/RECV flag
                        std::wstring flag = line.substr(separator1 + 1, separator2 - separator1 - 1);
                        msg.isSent = (flag == L"SENT");
                        msg.content = line.substr(separator2 + 1);
                    }
                    else
                    {
                        // Old format without flag (assume received)
                        msg.isSent = false;
                        msg.content = line.substr(separator1 + 1);
                    }

                    msg.isDecrypted = true;
                    m_userMessages[user].push_back(msg);
                }
            }

            file.close();
            OutputDebugStringW((L"[Discrypt] Loaded " + std::to_wstring(m_userMessages[user].size()) + L" messages for user: " + user + L"\n").c_str());
        }
        catch (...)
        {
            OutputDebugStringW(L"[Discrypt] Exception loading messages\n");
        }
    }

    void MainWindow::UpdateHandshakeStatus()
    {
        // Update on UI thread
        DispatcherQueue().TryEnqueue([this]()
        {
            // Update home view status
            UpdateHomeView();

            // Update chat view status if user is selected
            if (!m_currentUser.empty() && m_currentUser != L"home")
            {
                if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
                {
                    HandshakeStatusDisplay().Text(L"✓ Handshake complete");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 255, 0 }));
                }
                else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeInitiated)
                {
                    HandshakeStatusDisplay().Text(L"⏳ Handshake initiated");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 165, 0 }));
                }
                else
                {
                    HandshakeStatusDisplay().Text(L"⚠ No handshake - Use Alt+Enter in Discord to initiate");
                    HandshakeStatusDisplay().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 0, 0 }));
                }
            }
        });
    }

    void MainWindow::UpdateHomeView()
    {
        // Update handshake status in home view
        if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
        {
            HomeHandshakeStatus().Text(L"✓ Handshake complete - Ready to encrypt messages");
            HomeHandshakeStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 255, 0 }));
        }
        else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeInitiated)
        {
            HomeHandshakeStatus().Text(L"⏳ Handshake initiated - Waiting for partner response");
            HomeHandshakeStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 165, 0 }));
        }
        else
        {
            HomeHandshakeStatus().Text(L"⚠ No handshake - Press Alt+Enter in Discord to initiate");
            HomeHandshakeStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 0, 0 }));
        }

        // Update public key display
        if (!g_session.publicKeyBlob.empty())
        {
            std::wstring publicKeyHex = ::Discrypt::CryptoManager::GetPublicKeyHex(g_session);
            HomePublicKeyDisplay().Text(publicKeyHex.substr(0, (std::min)(publicKeyHex.length(), size_t(128))) + L"...");
        }
        else
        {
            HomePublicKeyDisplay().Text(L"No key generated yet");
        }
    }

    void MainWindow::AddSentMessageToHistory(const std::wstring& senderHandle, const std::wstring& message)
    {
        // Run on UI thread
        DispatcherQueue().TryEnqueue([this, senderHandle, message]()
        {
            // Extract just the username from @handle for the user key
            std::wstring user = senderHandle;
            if (!user.empty() && user[0] == L'@')
            {
                user = user.substr(1); // Remove @ prefix for display
            }

            // Get current timestamp
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &time);

            std::wostringstream oss;
            oss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
            std::wstring timestamp = oss.str();

            // Create message data
            MessageData msg;
            msg.timestamp = timestamp;
            msg.content = message;
            msg.isDecrypted = true;
            msg.isSent = true;

            // Add to history
            AddMessageToHistory(user, msg);

            // Save to file
            SaveMessagesToFile(user);

            // Update display if currently viewing this user
            if (m_currentUser == user)
            {
                UpdateMessageHistory();
            }

            OutputDebugStringW((L"[Discrypt] Added sent message to history for user: " + user + L"\n").c_str());
        });
    }

    void MainWindow::AddReceivedMessageToHistory(const std::wstring& senderHandle, const std::wstring& message)
    {
        // Run on UI thread
        DispatcherQueue().TryEnqueue([this, senderHandle, message]()
        {
            // Extract just the username from @handle for the user key
            std::wstring user = senderHandle;
            if (!user.empty() && user[0] == L'@')
            {
                user = user.substr(1); // Remove @ prefix for display
            }

            // Get current timestamp
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &time);

            std::wostringstream oss;
            oss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
            std::wstring timestamp = oss.str();

            // Create message data
            MessageData msg;
            msg.timestamp = timestamp;
            msg.content = message;
            msg.isDecrypted = true;
            msg.isSent = false; // Received message

            // Add to history
            AddMessageToHistory(user, msg);

            // Save to file
            SaveMessagesToFile(user);

            // Update display if currently viewing this user
            if (m_currentUser == user)
            {
                UpdateMessageHistory();
            }

            OutputDebugStringW((L"[Discrypt] Added received message to history for user: " + user + L"\n").c_str());
        });
    }

    void MainWindow::ClearAllMessages_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        // Create a content dialog to confirm deletion
        Controls::ContentDialog dialog;
        dialog.XamlRoot(this->Content().XamlRoot());
        dialog.Title(box_value(L"Clear All Messages"));
        dialog.Content(box_value(L"Are you sure you want to delete all message history for all users? This cannot be undone."));
        dialog.PrimaryButtonText(L"Delete All");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(Controls::ContentDialogButton::Close);

        auto asyncOp = dialog.ShowAsync();
        asyncOp.Completed([this](auto const& sender, auto const&)
            {
                auto result = sender.GetResults();
                if (result == Controls::ContentDialogResult::Primary)
                {
                    // Clear all in-memory messages
                    m_userMessages.clear();

                    // Delete all message files - use Windows API for temp path
                    wchar_t tempPathBuffer[MAX_PATH];
                    GetTempPathW(MAX_PATH, tempPathBuffer);
                    std::wstring tempPathStr = tempPathBuffer;
                    std::wstring appDataPath = tempPathStr + L"Discrypt\\";

                    // Check if directory exists and iterate through files
                    WIN32_FIND_DATAW findData;
                    std::wstring searchPath = appDataPath + L"*.txt";
                    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

                    if (hFind != INVALID_HANDLE_VALUE)
                    {
                        do
                        {
                            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                            {
                                std::wstring filePath = appDataPath + findData.cFileName;
                                if (DeleteFileW(filePath.c_str()))
                                {
                                    OutputDebugStringW((L"[Discrypt] Deleted: " + filePath + L"\n").c_str());
                                }
                                else
                                {
                                    OutputDebugStringW((L"[Discrypt] Failed to delete: " + filePath + L"\n").c_str());
                                }
                            }
                        } while (FindNextFileW(hFind, &findData));
                        FindClose(hFind);
                    }

                    // Update UI
                    DispatcherQueue().TryEnqueue([this]()
                        {
                            UpdateMessageHistory();

                            // Show success notification
                            Controls::ContentDialog successDialog;
                            successDialog.XamlRoot(this->Content().XamlRoot());
                            successDialog.Title(box_value(L"Success"));
                            successDialog.Content(box_value(L"All message history has been cleared."));
                            successDialog.CloseButtonText(L"OK");
                            successDialog.ShowAsync();
                        });
                }
            });
    }
}
