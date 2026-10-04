#pragma once

#include "App.xaml.g.h"

namespace winrt::PdfEditor::App::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        Microsoft::UI::Xaml::Window window_{ nullptr };
    };
}

namespace winrt::PdfEditor::App::factory_implementation
{
    struct App : AppT<App, implementation::App> {};
}

