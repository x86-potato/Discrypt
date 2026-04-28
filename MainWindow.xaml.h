#pragma once

#include "MainWindow.g.h"

namespace winrt::Discrypt::implementation
{
	struct MainWindow : MainWindowT<MainWindow>
	{
		MainWindow() = default;

		// Event handlers
		void InjectDiscordButton_Click(winrt::Windows::Foundation::IInspectable const& sender, 
									  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void ClearDataButton_Click(winrt::Windows::Foundation::IInspectable const& sender, 
								  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

		// Helper methods
		void UpdateHandshakeStatus();
		void UpdateInjectionStatus(bool isInjected);
	};
}

namespace winrt::Discrypt::factory_implementation
{
	struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
	{
	};
}
