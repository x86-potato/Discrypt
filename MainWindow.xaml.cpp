#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "CryptoManager.h"
#include "DatabaseManager.h"
#include "JSInjection.h"
#include "App.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// External declarations
extern ::Discrypt::EncryptionSession g_session;
extern ::Discrypt::DatabaseManager g_database;

namespace winrt::Discrypt::implementation
{
	void MainWindow::InjectDiscordButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		OutputDebugStringW(L"[Discrypt] Manual Discord injection requested...\n");

		// Update status
		InjectionStatus().Text(L"⏳ Injecting...");

		// Run injection in background thread
		std::thread([this]() {
			::Discrypt::DiscordInjector jsInjector;
			bool success = jsInjector.Inject();

			// Update UI on dispatcher thread
			DispatcherQueue().TryEnqueue([this, success]()
			{
				if (success)
				{
					OutputDebugStringW(L"[Discrypt] Manual Discord injection completed successfully\n");
					InjectionStatus().Text(L"✓ Injection successful");
					InjectionStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 255, 0 }));

					ContentDialog dialog;
					dialog.XamlRoot(this->Content().XamlRoot());
					dialog.Title(box_value(L"Success"));
					dialog.Content(box_value(L"Discord injection completed successfully."));
					dialog.CloseButtonText(L"OK");
					dialog.ShowAsync();
				}
				else
				{
					OutputDebugStringW(L"[Discrypt] Manual Discord injection failed\n");
					InjectionStatus().Text(L"❌ Injection failed");
					InjectionStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 0, 0 }));

					ContentDialog dialog;
					dialog.XamlRoot(this->Content().XamlRoot());
					dialog.Title(box_value(L"Injection Failed"));
					dialog.Content(box_value(L"Failed to inject into Discord. Make sure Discord is running."));
					dialog.CloseButtonText(L"OK");
					dialog.ShowAsync();
				}
			});
		}).detach();
	}

	void MainWindow::ClearDataButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ContentDialog dialog;
		dialog.XamlRoot(this->Content().XamlRoot());
		dialog.Title(box_value(L"Clear All Data"));
		dialog.Content(box_value(L"Are you sure you want to delete all saved data? This includes message history and conversations. This cannot be undone."));
		dialog.PrimaryButtonText(L"Delete All");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Close);

		auto asyncOp = dialog.ShowAsync();
		asyncOp.Completed([this](auto const& sender, auto const&)
		{
			auto result = sender.GetResults();
			if (result == ContentDialogResult::Primary)
			{
				OutputDebugStringW(L"[Discrypt] Clearing all data...\n");

				// Clear database
				if (g_database.ClearAllData())
				{
					OutputDebugStringW(L"[Discrypt] Database cleared successfully\n");
				}

				// Show success notification
				DispatcherQueue().TryEnqueue([this]()
				{
					ContentDialog successDialog;
					successDialog.XamlRoot(this->Content().XamlRoot());
					successDialog.Title(box_value(L"Success"));
					successDialog.Content(box_value(L"All data has been cleared."));
					successDialog.CloseButtonText(L"OK");
					successDialog.ShowAsync();
				});
			}
		});
	}

	void MainWindow::UpdateHandshakeStatus()
	{
		// Method kept for compatibility but no UI element to update
		DispatcherQueue().TryEnqueue([this]()
		{
			if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeComplete)
			{
				OutputDebugStringW(L"[Discrypt] Handshake complete\n");
			}
			else if (g_session.state == ::Discrypt::EncryptionSession::State::HandshakeInitiated)
			{
				OutputDebugStringW(L"[Discrypt] Handshake initiated\n");
			}
			else
			{
				OutputDebugStringW(L"[Discrypt] No active handshake\n");
			}
		});
	}

	void MainWindow::UpdateInjectionStatus(bool isInjected)
	{
		DispatcherQueue().TryEnqueue([this, isInjected]()
		{
			if (isInjected)
			{
				InjectionStatus().Text(L"✓ Discord injection active");
				InjectionStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 0, 255, 0 }));
			}
			else
			{
				InjectionStatus().Text(L"❌ Discord not found or injection failed");
				InjectionStatus().Foreground(Media::SolidColorBrush(Windows::UI::Color{ 255, 255, 0, 0 }));
			}
		});
	}
}

