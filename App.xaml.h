#pragma once

#include "App.xaml.g.h"

// Forward declaration
namespace Discrypt
{
    class MessageMonitor;
}

namespace winrt::Discrypt::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        // Message monitoring
        void StartMessageMonitoring();
        void StopMessageMonitoring();

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
        ::Discrypt::MessageMonitor* m_messageMonitor{ nullptr };
    };
}
