#pragma once

#include "MainWindow.g.h"

namespace winrt::Discrypt::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        // Event handlers
        void StatusSwitch_Toggled(winrt::Windows::Foundation::IInspectable const& sender, 
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void KeyInput_PasswordChanged(winrt::Windows::Foundation::IInspectable const& sender, 
                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RefreshButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    };
}

namespace winrt::Discrypt::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
