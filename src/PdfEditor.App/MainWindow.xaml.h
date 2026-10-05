#pragma once

#include "MainWindow.xaml.g.h"

#include "PdfEditor/Core/DocumentSession.h"
#include "PdfEditor/Core/MuPdfBackend.h"
#include "PdfEditor/Core/PdfWorker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::PdfEditor::App::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnOpen(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnSave(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnSaveAs(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUndo(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRedo(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSelectTool(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTextTool(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleThumbnails(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnZoomChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnSelectionFormattingChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnToggleFormattingChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNumberFormattingChanged(Microsoft::UI::Xaml::Controls::NumberBox const&, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void OnThumbnailSelected(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnDocumentViewChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        winrt::fire_and_forget OnDocumentPointerWheelChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        winrt::fire_and_forget OnKeyDown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        winrt::fire_and_forget OnAbout(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        struct PageView
        {
            Microsoft::UI::Xaml::Controls::Border shell{ nullptr };
            Microsoft::UI::Xaml::Controls::Grid surface{ nullptr };
            Microsoft::UI::Xaml::Controls::Image image{ nullptr };
            Microsoft::UI::Xaml::Controls::Canvas overlay{ nullptr };
            Microsoft::UI::Xaml::Controls::Image thumbnail{ nullptr };
            double fullRenderScale{};
            bool fullRendering{};
            bool thumbnailRendered{};
            bool thumbnailRendering{};
        };

        struct DragState
        {
            std::size_t pageIndex{};
            Windows::Foundation::Point start{};
            Microsoft::UI::Xaml::Controls::Border preview{ nullptr };
            bool active{};
        };

        void InitializeBackend();
        void InitializeColorPickers();
        Windows::Foundation::IAsyncOperation<bool> ConfirmDiscardAsync();
        Windows::Foundation::IAsyncAction OpenPdfAsync(std::filesystem::path path);
        Windows::Foundation::IAsyncAction SavePdfAsync(bool forceSaveAs);
        Windows::Foundation::IAsyncAction RenderAllAsync();
        Windows::Foundation::IAsyncAction RenderPageAsync(std::size_t pageIndex, bool thumbnail);
        winrt::fire_and_forget RenderVisiblePagesAsync();
        winrt::fire_and_forget RenderTextBoxPreviewAsync(std::string id);
        Windows::Foundation::IAsyncAction ValidateBoxAsync(std::string id);
        Windows::Foundation::IAsyncAction ShowMessageAsync(std::wstring title, std::wstring message);

        void BuildPageViews();
        void RebuildAllOverlays();
        void RebuildOverlay(std::size_t pageIndex);
        void UpdateBoxVisual(const pdfeditor::core::TextBoxModel& box);
        void UpdateOverflowVisual(std::string_view id, std::size_t pageIndex, bool overflow);
        void SyncFormattingControls();
        void ApplyFormattingFromControls();
        void RefreshCommandState();
        void UpdatePageSizes();
        void SetZoom(double percent);
        void SelectBox(std::string id);
        void FocusSelectedEditor();
        void DeleteSelection();
        void NudgeSelection(double viewDx, double viewDy);

        void OnPagePointerPressed(std::size_t pageIndex, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnPagePointerMoved(std::size_t pageIndex, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnPagePointerReleased(std::size_t pageIndex, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void BeginMove(std::string id);
        void MoveBox(std::string id, double dx, double dy);
        void ResizeBox(std::string id, double dx, double dy);
        void EndTransform();

        [[nodiscard]] double DipScale() const;
        [[nodiscard]] double RenderScale() const;
        [[nodiscard]] std::vector<std::size_t> VisiblePageIndices();
        [[nodiscard]] std::optional<std::size_t> SelectedPageIndex() const;

        std::shared_ptr<pdfeditor::core::MuPdfBackend> backend_;
        std::unique_ptr<pdfeditor::core::PdfWorker> worker_;
        std::unique_ptr<pdfeditor::core::DocumentSession> session_;
        pdfeditor::core::CancellationToken renderCancellation_;
        std::vector<PageView> pageViews_;
        DragState drag_;
        double zoomPercent_{ 100.0 };
        bool fitWidth_{ true };
        bool fitPage_{};
        bool textTool_{};
        bool suppressFormatting_{};
        bool initialized_{};
        bool transformTransaction_{};
        bool synchronizingThumbnail_{};
        bool allowClose_{};
        std::uint64_t renderGeneration_{};
        Microsoft::UI::Windowing::AppWindow appWindow_{ nullptr };
    };
}

namespace winrt::PdfEditor::App::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
