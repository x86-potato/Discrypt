#pragma once

#include "App.xaml.g.h"

namespace winrt::Discrypt::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        // PromptForUserHandle() is deprecated - username is now entered via homepage UI

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };

    };
}
