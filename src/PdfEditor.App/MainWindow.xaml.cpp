#include "pch.h"
#include "MainWindow.xaml.h"

#include "PdfEditor/Core/Geometry.h"

#include <microsoft.ui.interop.h>
#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Storage::Pickers;
using namespace Windows::System;
using namespace Windows::UI;
using namespace Windows::UI::Text;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace core = pdfeditor::core;

namespace
{
    struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) IBufferByteAccess : ::IUnknown
    {
        virtual HRESULT __stdcall Buffer(std::uint8_t** value) = 0;
    };

    struct NamedColor
    {
        wchar_t const* name;
        core::Color color;
    };

    constexpr std::array<NamedColor, 7> Palette{
        NamedColor{ L"Black", { 0, 0, 0 } },
        NamedColor{ L"White", { 255, 255, 255 } },
        NamedColor{ L"Red", { 210, 45, 45 } },
        NamedColor{ L"Blue", { 20, 95, 210 } },
        NamedColor{ L"Green", { 20, 135, 75 } },
        NamedColor{ L"Yellow", { 255, 224, 80 } },
        NamedColor{ L"Gray", { 112, 112, 112 } },
    };

    Color UiColor(const core::Color& color, std::uint8_t alpha = 255)
    {
        return Color{ alpha, color.red, color.green, color.blue };
    }

    SolidColorBrush Brush(const core::Color& color, std::uint8_t alpha = 255)
    {
        return SolidColorBrush(UiColor(color, alpha));
    }

    std::u16string ToUtf16(hstring const& value)
    {
        std::u16string result;
        result.reserve(value.size());
        for (const wchar_t character : value)
        {
            result.push_back(static_cast<char16_t>(character));
        }
        return result;
    }

    std::filesystem::path ExecutableDirectory()
    {
        std::vector<wchar_t> buffer(512, L'\0');
        for (;;)
        {
            ::SetLastError(ERROR_SUCCESS);
            const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                throw std::runtime_error("Windows could not determine the application directory.");
            }
            if (length < buffer.size() && buffer[length] == L'\0')
            {
                return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
            }
            if (buffer.size() >= 32768)
            {
                throw std::runtime_error("The application path is too long.");
            }
            buffer.resize(std::min<std::size_t>(buffer.size() * 2, 32768), L'\0');
        }
    }

#if defined(PDFEDITOR_PORTABLE)
    std::wstring FileUri(std::filesystem::path const& path)
    {
        const auto generic = std::filesystem::absolute(path).lexically_normal().generic_wstring();
        std::wstring escaped;
        escaped.reserve(generic.size() + 16);
        for (const wchar_t character : generic)
        {
            switch (character)
            {
            case L'%': escaped += L"%25"; break;
            case L' ': escaped += L"%20"; break;
            case L'#': escaped += L"%23"; break;
            case L'?': escaped += L"%3F"; break;
            default: escaped.push_back(character); break;
            }
        }
        if (escaped.rfind(L"//", 0) == 0)
        {
            return L"file:" + escaped;
        }
        return L"file:///" + escaped;
    }
#endif

    hstring ToHString(std::u16string const& value)
    {
        std::wstring result;
        result.reserve(value.size());
        for (const char16_t character : value)
        {
            result.push_back(static_cast<wchar_t>(character));
        }
        return hstring(result);
    }

    hstring FontUri(core::TextBoxModel const& box)
    {
        std::wstring fileFamily;
        std::wstring displayFamily;
        switch (box.textStyle.family)
        {
        case core::FontFamily::notoSerif:
            fileFamily = L"NotoSerif";
            displayFamily = L"Noto Serif";
            break;
        case core::FontFamily::notoSansMono:
            fileFamily = L"NotoSansMono";
            displayFamily = L"Noto Sans Mono";
            break;
        default:
            fileFamily = L"NotoSans";
            displayFamily = L"Noto Sans";
            break;
        }
        // Noto Sans Mono has upright Regular/Bold masters only. WinUI and
        // MuPDF synthesize the requested italic slant from those masters.
        if (box.textStyle.family == core::FontFamily::notoSansMono)
        {
            fileFamily += box.textStyle.bold ? L"-Bold" : L"-Regular";
        }
        else if (box.textStyle.bold && box.textStyle.italic) fileFamily += L"-BoldItalic";
        else if (box.textStyle.bold) fileFamily += L"-Bold";
        else if (box.textStyle.italic) fileFamily += L"-Italic";
        else fileFamily += L"-Regular";
#if defined(PDFEDITOR_PORTABLE)
        const auto fontPath = ExecutableDirectory() / L"Assets" / L"Fonts" / (fileFamily + L".ttf");
        return hstring(FileUri(fontPath) + L"#" + displayFamily);
#else
        return hstring(L"ms-appx:///Assets/Fonts/" + fileFamily + L".ttf#" + displayFamily);
#endif
    }

    core::Color SelectedColor(ComboBox const& combo, core::Color fallback)
    {
        const int index = combo.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(Palette.size())) return fallback;
        return Palette[static_cast<std::size_t>(index)].color;
    }

    int ColorIndex(const core::Color& color)
    {
        for (std::size_t index = 0; index < Palette.size(); ++index)
        {
            if (Palette[index].color == color) return static_cast<int>(index);
        }
        return 0;
    }

    void SetBitmap(Image const& image, const core::PixelBuffer& pixels)
    {
        if (pixels.width <= 0 || pixels.height <= 0 || pixels.pixels.empty()) return;
        WriteableBitmap bitmap(pixels.width, pixels.height);
        std::uint8_t* destination{};
        check_hresult(bitmap.PixelBuffer().as<IBufferByteAccess>()->Buffer(&destination));
        std::memcpy(destination, pixels.pixels.data(), pixels.pixels.size());
        bitmap.Invalidate();
        image.Source(bitmap);
    }

    bool KeyDown(int virtualKey)
    {
        return (::GetKeyState(virtualKey) & 0x8000) != 0;
    }
}

namespace winrt::PdfEditor::App::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"PDF Textbox Editor");
    }

    void MainWindow::OnLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        if (initialized_) return;
        initialized_ = true;

        InitializeColorPickers();
        try
        {
            InitializeBackend();
            StatusText().Text(L"Ready");
        }
        catch (std::exception const& error)
        {
            MessageBar().Severity(InfoBarSeverity::Error);
            MessageBar().Title(L"Startup failed");
            MessageBar().Message(to_hstring(error.what()));
            MessageBar().IsOpen(true);
            OpenButton().IsEnabled(false);
        }

        HWND hwnd{};
        if (auto windowNative = this->try_as<::IWindowNative>())
        {
            check_hresult(windowNative->get_WindowHandle(&hwnd));
            appWindow_ = AppWindow::GetFromWindowId(GetWindowIdFromWindow(hwnd));
            appWindow_.Closing([strong = get_strong()](AppWindow const&, AppWindowClosingEventArgs const& args)
            {
                if (!strong->session_ || !strong->session_->dirty() || strong->allowClose_) return;
                args.Cancel(true);
                ContentDialog dialog;
                dialog.XamlRoot(strong->RootGrid().XamlRoot());
                dialog.Title(box_value(L"Discard unsaved textboxes?"));
                dialog.Content(box_value(L"The original PDF is unchanged, but edits in this session will be lost."));
                dialog.PrimaryButtonText(L"Discard");
                dialog.CloseButtonText(L"Keep editing");
                auto operation = dialog.ShowAsync();
                operation.Completed([strong](auto const& completed, AsyncStatus status)
                {
                    if (status == AsyncStatus::Completed && completed.GetResults() == ContentDialogResult::Primary)
                    {
                        strong->allowClose_ = true;
                        strong->Close();
                    }
                });
            });
        }
        RootGrid().Focus(FocusState::Programmatic);
    }

    void MainWindow::InitializeBackend()
    {
        const auto fontDirectory = ExecutableDirectory() / L"Assets" / L"Fonts";
        if (!std::filesystem::is_directory(fontDirectory))
        {
            throw std::runtime_error("The bundled font directory is missing. Extract the complete application folder before running it.");
        }
        backend_ = std::make_shared<core::MuPdfBackend>(fontDirectory);
        worker_ = std::make_unique<core::PdfWorker>();
    }

    fire_and_forget MainWindow::OnAbout(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime = get_strong();
        co_await ShowMessageAsync(
            L"PDF Textbox Editor 0.1.0",
            L"This program is free software licensed under the GNU Affero General Public License, version 3 or later, and is provided without warranty.\n\n"
            L"Source code: https://github.com/monoIith/pdfbeamer\n\n"
            L"PDF support is provided by MuPDF 1.28.5. Bundled Noto fonts are licensed under the SIL Open Font License 1.1.");
    }

    void MainWindow::InitializeColorPickers()
    {
        for (auto const& entry : Palette)
        {
            TextColorCombo().Items().Append(box_value(entry.name));
            BorderColorCombo().Items().Append(box_value(entry.name));
            FillColorCombo().Items().Append(box_value(entry.name));
        }
        TextColorCombo().SelectedIndex(0);
        BorderColorCombo().SelectedIndex(0);
        FillColorCombo().SelectedIndex(1);
    }

    IAsyncOperation<bool> MainWindow::ConfirmDiscardAsync()
    {
        if (!session_ || !session_->dirty()) co_return true;
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Discard unsaved textboxes?"));
        dialog.Content(box_value(L"Opening another PDF will discard the edits in this session."));
        dialog.PrimaryButtonText(L"Discard");
        dialog.CloseButtonText(L"Cancel");
        co_return co_await dialog.ShowAsync() == ContentDialogResult::Primary;
    }

    fire_and_forget MainWindow::OnOpen(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime = get_strong();
        try
        {
            if (!co_await ConfirmDiscardAsync()) co_return;
            FileOpenPicker picker;
            picker.FileTypeFilter().Append(L".pdf");
            HWND hwnd{};
            check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            auto file = co_await picker.PickSingleFileAsync();
            if (!file) co_return;
            co_await OpenPdfAsync(std::filesystem::path(file.Path().c_str()));
        }
        catch (std::exception const& error)
        {
            co_await ShowMessageAsync(L"Could not open PDF", to_hstring(error.what()).c_str());
        }
    }

    IAsyncAction MainWindow::OpenPdfAsync(std::filesystem::path path)
    {
        const auto dispatcher = DispatcherQueue();
        StatusText().Text(L"Opening PDF…");
        MessageBar().IsOpen(false);
        renderCancellation_.cancel();
        renderCancellation_ = core::CancellationToken{};
        auto future = worker_->submit([backend = backend_, path]
        {
            return backend->open(path);
        });

        co_await resume_background();
        auto opened = future.get();
        co_await resume_foreground(dispatcher);

        session_ = std::make_unique<core::DocumentSession>(path, opened.pages, opened.hasDigitalSignatures);
        BuildPageViews();
        if (opened.hasDigitalSignatures)
        {
            MessageBar().Severity(InfoBarSeverity::Warning);
            MessageBar().Title(L"Signed PDF");
            MessageBar().Message(L"The original remains unchanged, but signatures will not be valid in an edited copy.");
            MessageBar().IsOpen(true);
        }
        else if (opened.wasRepaired)
        {
            MessageBar().Severity(InfoBarSeverity::Warning);
            MessageBar().Title(L"PDF repaired while opening");
            MessageBar().Message(L"Review the saved copy carefully because the source contained structural errors.");
            MessageBar().IsOpen(true);
        }
        StatusText().Text(L"Rendering pages…");
        RefreshCommandState();
        co_await RenderAllAsync();
        StatusText().Text(L"Ready — choose Text and drag a rectangle on any page.");
    }

    fire_and_forget MainWindow::OnSave(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime = get_strong();
        try
        {
            co_await SavePdfAsync(false);
        }
        catch (std::exception const& error)
        {
            co_await ShowMessageAsync(L"Could not save PDF", to_hstring(error.what()).c_str());
        }
    }

    fire_and_forget MainWindow::OnSaveAs(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime = get_strong();
        try
        {
            co_await SavePdfAsync(true);
        }
        catch (std::exception const& error)
        {
            co_await ShowMessageAsync(L"Could not save PDF", to_hstring(error.what()).c_str());
        }
    }

    IAsyncAction MainWindow::SavePdfAsync(bool forceSaveAs)
    {
        if (!session_) co_return;
        session_->commitTransaction();
        session_->removeEmptyTextBoxes();
        RebuildAllOverlays();
        if (session_->hasOverflow())
        {
            co_await ShowMessageAsync(L"Text does not fit",
                L"Resize the red textbox or reduce its font size before saving.");
            co_return;
        }

        std::filesystem::path destination;
        if (!forceSaveAs && session_->outputPath())
        {
            destination = *session_->outputPath();
        }
        else
        {
            FileSavePicker picker;
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.SuggestedFileName(hstring(session_->sourcePath().stem().wstring() + L"-edited"));
            picker.FileTypeChoices().Insert(
                L"PDF document", single_threaded_vector<hstring>(std::vector<hstring>{ L".pdf" }));
            HWND hwnd{};
            check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            auto file = co_await picker.PickSaveFileAsync();
            if (!file) co_return;
            destination = std::filesystem::path(file.Path().c_str());
        }

        if (session_->hasDigitalSignatures())
        {
            ContentDialog warning;
            warning.XamlRoot(RootGrid().XamlRoot());
            warning.Title(box_value(L"Create an unsigned edited copy?"));
            warning.Content(box_value(L"Any signatures in the source will not validate in the edited output."));
            warning.PrimaryButtonText(L"Save copy");
            warning.CloseButtonText(L"Cancel");
            if (co_await warning.ShowAsync() != ContentDialogResult::Primary) co_return;
        }

        const auto dispatcher = DispatcherQueue();
        StatusText().Text(L"Saving and validating PDF…");
        SaveButton().IsEnabled(false);
        SaveAsButton().IsEnabled(false);
        const auto source = session_->sourcePath();
        const auto pages = session_->pages();
        const auto boxes = session_->textBoxes();
        core::CancellationToken cancellation;
        auto future = worker_->submit([backend = backend_, source, destination, pages, boxes, cancellation]
        {
            return backend->saveAs(source, destination, pages, boxes, cancellation);
        });

        co_await resume_background();
        const auto result = future.get();
        co_await resume_foreground(dispatcher);
        session_->markSaved(result.path);
        StatusText().Text(L"Saved " + hstring(result.path.filename().wstring()));
        RefreshCommandState();
    }

    void MainWindow::BuildPageViews()
    {
        ++renderGeneration_;
        PagesPanel().Children().Clear();
        ThumbnailList().Items().Clear();
        pageViews_.clear();
        if (!session_) return;

        pageViews_.reserve(session_->pages().size());
        for (std::size_t index = 0; index < session_->pages().size(); ++index)
        {
            Grid surface;
            surface.Background(SolidColorBrush(Color{ 255, 255, 255, 255 }));

            Image pageImage;
            pageImage.Stretch(Stretch::Fill);
            surface.Children().Append(pageImage);

            Canvas overlay;
            overlay.Background(SolidColorBrush(Color{ 0, 0, 0, 0 }));
            overlay.PointerPressed([this, index](IInspectable const&, PointerRoutedEventArgs const& args)
            {
                OnPagePointerPressed(index, args);
            });
            overlay.PointerMoved([this, index](IInspectable const&, PointerRoutedEventArgs const& args)
            {
                OnPagePointerMoved(index, args);
            });
            overlay.PointerReleased([this, index](IInspectable const&, PointerRoutedEventArgs const& args)
            {
                OnPagePointerReleased(index, args);
            });
            surface.Children().Append(overlay);

            Border shell;
            shell.Child(surface);
            shell.Background(SolidColorBrush(Color{ 255, 255, 255, 255 }));
            shell.BorderBrush(SolidColorBrush(Color{ 64, 0, 0, 0 }));
            shell.BorderThickness(Thickness{ 1 });
            shell.Tag(box_value(static_cast<int>(index)));
            PagesPanel().Children().Append(shell);

            StackPanel thumbnailPanel;
            thumbnailPanel.Spacing(5);
            thumbnailPanel.Padding(Thickness{ 8 });
            thumbnailPanel.HorizontalAlignment(HorizontalAlignment::Center);
            Image thumbnail;
            thumbnail.Width(116);
            thumbnail.Height(150);
            thumbnail.Stretch(Stretch::Uniform);
            thumbnailPanel.Children().Append(thumbnail);
            TextBlock number;
            number.Text(L"Page " + to_hstring(index + 1));
            number.HorizontalAlignment(HorizontalAlignment::Center);
            thumbnailPanel.Children().Append(number);
            ThumbnailList().Items().Append(thumbnailPanel);

            pageViews_.push_back({ shell, surface, pageImage, overlay, thumbnail });
        }

        EmptyState().Visibility(Visibility::Collapsed);
        ThumbnailList().SelectedIndex(0);
        UpdatePageSizes();
        RebuildAllOverlays();
        PageStatusText().Text(L"Page 1 of " + to_hstring(session_->pages().size()));
    }

    double MainWindow::DipScale() const
    {
        return (zoomPercent_ / 100.0) * (96.0 / 72.0);
    }

    double MainWindow::RenderScale() const
    {
        const double rasterScale = RootGrid().XamlRoot() ? RootGrid().XamlRoot().RasterizationScale() : 1.0;
        return DipScale() * rasterScale;
    }

    void MainWindow::UpdatePageSizes()
    {
        if (!session_) return;
        const double scale = DipScale();
        for (std::size_t index = 0; index < pageViews_.size(); ++index)
        {
            const auto size = core::geometry::RotatedSize(session_->pages()[index]);
            pageViews_[index].surface.Width(size.width * scale);
            pageViews_[index].surface.Height(size.height * scale);
            pageViews_[index].overlay.Width(size.width * scale);
            pageViews_[index].overlay.Height(size.height * scale);
        }
        RebuildAllOverlays();
    }

    IAsyncAction MainWindow::RenderAllAsync()
    {
        if (!session_) co_return;
        for (const auto index : VisiblePageIndices())
        {
            co_await RenderPageAsync(index, false);
        }
        for (std::size_t index = 0; index < pageViews_.size(); ++index)
        {
            co_await RenderPageAsync(index, true);
        }
    }

    std::vector<std::size_t> MainWindow::VisiblePageIndices()
    {
        std::vector<std::size_t> result;
        const double offset = DocumentScroll().VerticalOffset();
        const double viewport = std::max(300.0, DocumentScroll().ActualHeight());
        double top = 32.0;
        for (std::size_t index = 0; index < pageViews_.size(); ++index)
        {
            const double height = pageViews_[index].surface.Height();
            const bool nearViewport = top + height >= offset - viewport
                && top <= offset + viewport * 2.0;
            if (nearViewport)
            {
                result.push_back(index);
            }
            else if (!pageViews_[index].fullRendering)
            {
                pageViews_[index].image.Source(ImageSource{ nullptr });
                pageViews_[index].fullRenderScale = 0.0;
            }
            top += height + 24.0;
        }
        return result;
    }

    fire_and_forget MainWindow::RenderVisiblePagesAsync()
    {
        auto lifetime = get_strong();
        try
        {
            for (const auto index : VisiblePageIndices())
            {
                co_await RenderPageAsync(index, false);
            }
        }
        catch (std::exception const& error)
        {
            MessageBar().Severity(InfoBarSeverity::Warning);
            MessageBar().Title(L"Page rendering failed");
            MessageBar().Message(to_hstring(error.what()));
            MessageBar().IsOpen(true);
        }
    }

    IAsyncAction MainWindow::RenderPageAsync(std::size_t pageIndex, bool thumbnail)
    {
        if (!session_ || pageIndex >= pageViews_.size()) co_return;
        const auto dispatcher = DispatcherQueue();
        const double scale = thumbnail ? 0.22 : RenderScale();
        auto& pageView = pageViews_[pageIndex];
        if (thumbnail)
        {
            if (pageView.thumbnailRendered || pageView.thumbnailRendering) co_return;
            pageView.thumbnailRendering = true;
        }
        else
        {
            if (pageView.fullRendering || std::abs(pageView.fullRenderScale - scale) < 0.001) co_return;
            pageView.fullRendering = true;
        }
        const auto generation = renderGeneration_;
        const auto cancellation = renderCancellation_;
        auto future = worker_->submit([backend = backend_, pageIndex, scale, cancellation]
        {
            return backend->renderPage(pageIndex, scale, cancellation);
        });
        co_await resume_background();
        core::PixelBuffer pixels;
        std::exception_ptr failure;
        try
        {
            pixels = future.get();
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await resume_foreground(dispatcher);
        if (generation == renderGeneration_ && pageIndex < pageViews_.size())
        {
            if (thumbnail) pageViews_[pageIndex].thumbnailRendering = false;
            else pageViews_[pageIndex].fullRendering = false;
        }
        if (failure) std::rethrow_exception(failure);
        if (pixels.pixels.empty() || generation != renderGeneration_
            || !session_ || pageIndex >= pageViews_.size()) co_return;
        SetBitmap(thumbnail ? pageViews_[pageIndex].thumbnail : pageViews_[pageIndex].image, pixels);
        if (thumbnail) pageViews_[pageIndex].thumbnailRendered = true;
        else pageViews_[pageIndex].fullRenderScale = scale;
    }

    fire_and_forget MainWindow::RenderTextBoxPreviewAsync(std::string id)
    {
        auto lifetime = get_strong();
        const auto dispatcher = DispatcherQueue();
        try
        {
            if (!session_) co_return;
            const auto* current = session_->findTextBox(id);
            if (!current) co_return;
            const auto snapshot = *current;
            const auto page = session_->pages()[snapshot.pageIndex];
            const auto scale = RenderScale();
            const auto generation = renderGeneration_;
            const auto cancellation = renderCancellation_;
            auto future = worker_->submit([backend = backend_, snapshot, page, scale, cancellation]
            {
                return backend->renderTextBox(snapshot, page, scale, cancellation);
            });
            co_await resume_background();
            auto pixels = future.get();
            if (pixels.pixels.empty()) co_return;
            co_await resume_foreground(dispatcher);
            if (!session_ || generation != renderGeneration_
                || std::abs(scale - RenderScale()) >= 0.001
                || snapshot.pageIndex >= pageViews_.size()) co_return;
            const auto* latest = session_->findTextBox(id);
            if (!latest || latest->text != snapshot.text || latest->bounds != snapshot.bounds
                || latest->textStyle != snapshot.textStyle || latest->boxStyle != snapshot.boxStyle)
            {
                co_return;
            }
            for (auto const& child : pageViews_[snapshot.pageIndex].overlay.Children())
            {
                auto container = child.try_as<Grid>();
                if (!container || unbox_value_or<hstring>(container.Tag(), L"") != to_hstring(id)) continue;
                auto border = container.Children().GetAt(0).try_as<Border>();
                if (!border) co_return;
                if (auto preview = border.Child().try_as<Image>())
                {
                    SetBitmap(preview, pixels);
                }
                co_return;
            }
        }
        catch (std::exception const& error)
        {
            co_await resume_foreground(dispatcher);
            MessageBar().Severity(InfoBarSeverity::Warning);
            MessageBar().Title(L"Textbox preview failed");
            MessageBar().Message(to_hstring(error.what()));
            MessageBar().IsOpen(true);
        }
    }

    void MainWindow::RebuildAllOverlays()
    {
        for (std::size_t index = 0; index < pageViews_.size(); ++index)
        {
            RebuildOverlay(index);
        }
        SyncFormattingControls();
        RefreshCommandState();
    }

    void MainWindow::RebuildOverlay(std::size_t pageIndex)
    {
        if (!session_ || pageIndex >= pageViews_.size()) return;
        auto canvas = pageViews_[pageIndex].overlay;
        canvas.Children().Clear();
        const double scale = DipScale();

        for (auto const& box : session_->textBoxes())
        {
            if (box.pageIndex != pageIndex) continue;
            const auto view = core::geometry::ToView(session_->pages()[pageIndex], box.bounds);
            const bool selected = session_->selectedId() && *session_->selectedId() == box.id;

            Grid container;
            container.Width(view.width * scale);
            container.Height(view.height * scale);
            container.Tag(box_value(to_hstring(box.id)));
            Canvas::SetLeft(container, view.x * scale);
            Canvas::SetTop(container, view.y * scale);

            Border contentBorder;
            if (selected)
            {
                contentBorder.BorderBrush(box.overflow ? Brush({ 220, 35, 35 }) : Brush(box.boxStyle.borderColor));
                contentBorder.BorderThickness(Thickness{ std::max(box.overflow ? 2.0 : 0.0,
                    box.boxStyle.borderWidthPoints * scale) });
                const auto alpha = static_cast<std::uint8_t>(
                    std::clamp(box.boxStyle.fillOpacity, 0.0, 1.0) * 255.0);
                contentBorder.Background(Brush(box.boxStyle.fillColor, alpha));
            }
            else
            {
                contentBorder.BorderBrush(box.overflow ? Brush({ 220, 35, 35 }) : Brush({ 0, 0, 0 }, 0));
                contentBorder.BorderThickness(Thickness{ box.overflow ? 2.0 : 0.0 });
                contentBorder.Background(Brush({ 0, 0, 0 }, 0));
            }

            if (selected)
            {
                TextBox editor;
                editor.Text(ToHString(box.text));
                editor.AcceptsReturn(true);
                editor.TextWrapping(TextWrapping::Wrap);
                editor.Padding(Thickness{ box.boxStyle.paddingPoints * scale });
                editor.Background(SolidColorBrush(Color{ 0, 0, 0, 0 }));
                editor.BorderThickness(Thickness{ 0 });
                editor.Foreground(Brush(box.textStyle.color));
                editor.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(FontUri(box)));
                editor.FontSize(box.textStyle.sizePoints * scale);
                editor.FontWeight(box.textStyle.bold ? FontWeights::Bold() : FontWeights::Normal());
                editor.FontStyle(box.textStyle.italic ? FontStyle::Italic : FontStyle::Normal);
                editor.TextAlignment(box.textStyle.alignment == core::TextAlignment::center
                    ? TextAlignment::Center
                    : box.textStyle.alignment == core::TextAlignment::right ? TextAlignment::Right : TextAlignment::Left);
                editor.Tag(box_value(to_hstring(box.id)));
                editor.GotFocus([this](IInspectable const&, RoutedEventArgs const&)
                {
                    if (session_) session_->beginTransaction();
                });
                editor.LostFocus([this](IInspectable const&, RoutedEventArgs const&)
                {
                    if (!session_) return;
                    session_->commitTransaction();
                    session_->removeEmptyTextBoxes();
                    RebuildAllOverlays();
                });
                editor.TextChanged([this, id = box.id](IInspectable const& sender, TextChangedEventArgs const&)
                {
                    if (suppressFormatting_ || !session_) return;
                    auto* current = session_->findTextBox(id);
                    if (!current) return;
                    auto updated = *current;
                    updated.text = ToUtf16(sender.as<TextBox>().Text());
                    session_->updateTextBox(updated);
                    RefreshCommandState();
                    ValidateBoxAsync(id);
                });
                contentBorder.Child(editor);
            }
            else
            {
                Image preview;
                preview.Stretch(Stretch::Fill);
                contentBorder.Child(preview);
            }
            container.Children().Append(contentBorder);

            if (selected)
            {
                Border selection;
                selection.BorderBrush(Brush({ 0, 120, 212 }));
                selection.BorderThickness(Thickness{ 2 });
                selection.IsHitTestVisible(false);
                container.Children().Append(selection);

                Thumb move;
                move.Width(16);
                move.Height(16);
                move.Background(Brush({ 0, 120, 212 }));
                move.HorizontalAlignment(HorizontalAlignment::Left);
                move.VerticalAlignment(VerticalAlignment::Top);
                move.Margin(Thickness{ -7, -7, 0, 0 });
                ToolTipService::SetToolTip(move, box_value(L"Move textbox"));
                move.DragStarted([this, id = box.id](IInspectable const&, DragStartedEventArgs const&) { BeginMove(id); });
                move.DragDelta([this, id = box.id](IInspectable const&, DragDeltaEventArgs const& args)
                {
                    MoveBox(id, args.HorizontalChange(), args.VerticalChange());
                });
                move.DragCompleted([this](IInspectable const&, DragCompletedEventArgs const&) { EndTransform(); });
                container.Children().Append(move);

                Thumb resize;
                resize.Width(16);
                resize.Height(16);
                resize.Background(Brush({ 0, 120, 212 }));
                resize.HorizontalAlignment(HorizontalAlignment::Right);
                resize.VerticalAlignment(VerticalAlignment::Bottom);
                resize.Margin(Thickness{ 0, 0, -7, -7 });
                ToolTipService::SetToolTip(resize, box_value(L"Resize textbox"));
                resize.DragStarted([this, id = box.id](IInspectable const&, DragStartedEventArgs const&) { BeginMove(id); });
                resize.DragDelta([this, id = box.id](IInspectable const&, DragDeltaEventArgs const& args)
                {
                    ResizeBox(id, args.HorizontalChange(), args.VerticalChange());
                });
                resize.DragCompleted([this](IInspectable const&, DragCompletedEventArgs const&) { EndTransform(); });
                container.Children().Append(resize);
            }

            container.PointerPressed([this, id = box.id](IInspectable const&, PointerRoutedEventArgs const& args)
            {
                if (session_ && session_->selectedId() && *session_->selectedId() == id)
                {
                    return;
                }
                SelectBox(id);
                args.Handled(true);
            });
            canvas.Children().Append(container);
            if (!selected)
            {
                RenderTextBoxPreviewAsync(box.id);
            }
        }
    }

    void MainWindow::UpdateBoxVisual(const core::TextBoxModel& box)
    {
        if (!session_ || box.pageIndex >= pageViews_.size()) return;
        const auto view = core::geometry::ToView(session_->pages()[box.pageIndex], box.bounds);
        const double scale = DipScale();
        for (auto const& child : pageViews_[box.pageIndex].overlay.Children())
        {
            auto element = child.try_as<FrameworkElement>();
            if (!element || !element.Tag()) continue;
            auto tag = unbox_value_or<hstring>(element.Tag(), L"");
            if (tag != to_hstring(box.id)) continue;
            element.Width(view.width * scale);
            element.Height(view.height * scale);
            Canvas::SetLeft(element, view.x * scale);
            Canvas::SetTop(element, view.y * scale);
            break;
        }
    }

    void MainWindow::UpdateOverflowVisual(std::string_view id, std::size_t pageIndex, bool overflow)
    {
        if (!session_ || pageIndex >= pageViews_.size()) return;
        const auto* box = session_->findTextBox(id);
        if (!box) return;
        for (auto const& child : pageViews_[pageIndex].overlay.Children())
        {
            auto container = child.try_as<Grid>();
            if (!container || unbox_value_or<hstring>(container.Tag(), L"") != to_hstring(id)) continue;
            auto border = container.Children().GetAt(0).try_as<Border>();
            if (!border) return;
            const bool selected = session_->selectedId() && *session_->selectedId() == id;
            border.BorderBrush(overflow
                ? Brush({ 220, 35, 35 })
                : selected ? Brush(box->boxStyle.borderColor) : Brush({ 0, 0, 0 }, 0));
            border.BorderThickness(Thickness{ overflow
                ? 2.0
                : selected ? box->boxStyle.borderWidthPoints * DipScale() : 0.0 });
            return;
        }
    }

    void MainWindow::SelectBox(std::string id)
    {
        if (!session_) return;
        if (session_->selectedId() && *session_->selectedId() == id) return;
        session_->commitTransaction();
        session_->removeEmptyTextBoxes();
        if (!session_->findTextBox(id)) return;
        session_->select(std::move(id));
        RebuildAllOverlays();
        SyncFormattingControls();
    }

    void MainWindow::FocusSelectedEditor()
    {
        if (!session_ || !session_->selectedId()) return;
        const auto id = to_hstring(*session_->selectedId());
        for (auto const& page : pageViews_)
        {
            for (auto const& child : page.overlay.Children())
            {
                auto grid = child.try_as<Grid>();
                if (!grid || unbox_value_or<hstring>(grid.Tag(), L"") != id) continue;
                auto border = grid.Children().GetAt(0).try_as<Border>();
                if (border)
                {
                    if (auto editor = border.Child().try_as<TextBox>())
                    {
                        editor.Focus(FocusState::Programmatic);
                        editor.Select(editor.Text().size(), 0);
                    }
                }
                return;
            }
        }
    }

    IAsyncAction MainWindow::ValidateBoxAsync(std::string id)
    {
        if (!session_) co_return;
        const auto dispatcher = DispatcherQueue();
        const auto* current = session_->findTextBox(id);
        if (!current) co_return;
        const auto snapshot = *current;
        const auto page = session_->pages()[snapshot.pageIndex];
        auto future = worker_->submit([backend = backend_, snapshot, page]
        {
            return backend->validateTextBox(snapshot, page);
        });
        co_await resume_background();
        const auto result = future.get();
        co_await resume_foreground(dispatcher);
        if (!session_) co_return;
        const auto* latest = session_->findTextBox(id);
        if (!latest || latest->text != snapshot.text || latest->bounds != snapshot.bounds
            || latest->textStyle != snapshot.textStyle || latest->boxStyle != snapshot.boxStyle)
        {
            co_return;
        }
        session_->setOverflow(id, !result.fits);
        UpdateOverflowVisual(id, snapshot.pageIndex, !result.fits);
        RefreshCommandState();
        if (!result.fits)
        {
            MessageBar().Severity(InfoBarSeverity::Error);
            MessageBar().Title(L"Textbox overflow");
            MessageBar().Message(to_hstring(result.message));
            MessageBar().IsOpen(true);
        }
    }

    void MainWindow::OnPagePointerPressed(std::size_t pageIndex, PointerRoutedEventArgs const& args)
    {
        if (!session_) return;
        if (!textTool_)
        {
            session_->commitTransaction();
            session_->removeEmptyTextBoxes();
            session_->select(std::nullopt);
            RebuildAllOverlays();
            return;
        }
        const auto point = args.GetCurrentPoint(pageViews_[pageIndex].overlay);
        if (!point.Properties().IsLeftButtonPressed()) return;

        drag_.pageIndex = pageIndex;
        drag_.start = point.Position();
        drag_.active = true;
        Border preview;
        preview.BorderBrush(Brush({ 0, 120, 212 }));
        preview.BorderThickness(Thickness{ 2 });
        preview.Background(Brush({ 0, 120, 212 }, 24));
        preview.Width(1);
        preview.Height(1);
        Canvas::SetLeft(preview, drag_.start.X);
        Canvas::SetTop(preview, drag_.start.Y);
        pageViews_[pageIndex].overlay.Children().Append(preview);
        drag_.preview = preview;
        pageViews_[pageIndex].overlay.CapturePointer(args.Pointer());
        args.Handled(true);
    }

    void MainWindow::OnPagePointerMoved(std::size_t pageIndex, PointerRoutedEventArgs const& args)
    {
        if (!drag_.active || drag_.pageIndex != pageIndex || !drag_.preview) return;
        const auto position = args.GetCurrentPoint(pageViews_[pageIndex].overlay).Position();
        const double x = std::min<double>(drag_.start.X, position.X);
        const double y = std::min<double>(drag_.start.Y, position.Y);
        const double width = std::abs(position.X - drag_.start.X);
        const double height = std::abs(position.Y - drag_.start.Y);
        Canvas::SetLeft(drag_.preview, x);
        Canvas::SetTop(drag_.preview, y);
        drag_.preview.Width(width);
        drag_.preview.Height(height);
        args.Handled(true);
    }

    void MainWindow::OnPagePointerReleased(std::size_t pageIndex, PointerRoutedEventArgs const& args)
    {
        if (!drag_.active || drag_.pageIndex != pageIndex) return;
        const auto position = args.GetCurrentPoint(pageViews_[pageIndex].overlay).Position();
        pageViews_[pageIndex].overlay.ReleasePointerCapture(args.Pointer());
        const double scale = DipScale();
        core::Rect view{
            std::min<double>(drag_.start.X, position.X) / scale,
            std::min<double>(drag_.start.Y, position.Y) / scale,
            std::abs(position.X - drag_.start.X) / scale,
            std::abs(position.Y - drag_.start.Y) / scale,
        };
        drag_ = {};
        if (view.width < 24.0 || view.height < 16.0)
        {
            RebuildOverlay(pageIndex);
            return;
        }
        const auto bounds = core::geometry::FromView(session_->pages()[pageIndex], view);
        auto& box = session_->addTextBox(pageIndex, bounds);
        const auto id = box.id;
        RebuildAllOverlays();
        ValidateBoxAsync(id);
        DispatcherQueue().TryEnqueue([strong = get_strong()] { strong->FocusSelectedEditor(); });
        args.Handled(true);
    }

    void MainWindow::BeginMove(std::string)
    {
        if (!session_ || transformTransaction_) return;
        session_->beginTransaction();
        transformTransaction_ = true;
    }

    void MainWindow::MoveBox(std::string id, double dx, double dy)
    {
        if (!session_) return;
        auto* current = session_->findTextBox(id);
        if (!current) return;
        auto updated = *current;
        auto view = core::geometry::ToView(session_->pages()[updated.pageIndex], updated.bounds);
        view.x += dx / DipScale();
        view.y += dy / DipScale();
        updated.bounds = core::geometry::FromView(session_->pages()[updated.pageIndex], view);
        session_->updateTextBox(updated);
        UpdateBoxVisual(*session_->findTextBox(id));
    }

    void MainWindow::ResizeBox(std::string id, double dx, double dy)
    {
        if (!session_) return;
        auto* current = session_->findTextBox(id);
        if (!current) return;
        auto updated = *current;
        auto view = core::geometry::ToView(session_->pages()[updated.pageIndex], updated.bounds);
        view.width = std::max(24.0, view.width + dx / DipScale());
        view.height = std::max(16.0, view.height + dy / DipScale());
        const auto pageSize = core::geometry::RotatedSize(session_->pages()[updated.pageIndex]);
        view.width = std::min(view.width, pageSize.width - view.x);
        view.height = std::min(view.height, pageSize.height - view.y);
        updated.bounds = core::geometry::FromView(session_->pages()[updated.pageIndex], view);
        session_->updateTextBox(updated);
        UpdateBoxVisual(*session_->findTextBox(id));
    }

    void MainWindow::EndTransform()
    {
        if (!session_ || !transformTransaction_) return;
        transformTransaction_ = false;
        session_->commitTransaction();
        if (session_->selectedId()) ValidateBoxAsync(*session_->selectedId());
        RebuildAllOverlays();
    }

    void MainWindow::SyncFormattingControls()
    {
        suppressFormatting_ = true;
        const core::TextBoxModel* box = nullptr;
        if (session_ && session_->selectedId()) box = session_->findTextBox(*session_->selectedId());
        FormatBar().IsEnabled(box != nullptr);
        if (box)
        {
            FontCombo().SelectedIndex(static_cast<int>(box->textStyle.family));
            FontSizeBox().Value(box->textStyle.sizePoints);
            BoldButton().IsChecked(box->textStyle.bold);
            ItalicButton().IsChecked(box->textStyle.italic);
            AlignmentCombo().SelectedIndex(static_cast<int>(box->textStyle.alignment));
            TextColorCombo().SelectedIndex(ColorIndex(box->textStyle.color));
            BorderColorCombo().SelectedIndex(ColorIndex(box->boxStyle.borderColor));
            BorderWidthBox().Value(box->boxStyle.borderWidthPoints);
            FillColorCombo().SelectedIndex(ColorIndex(box->boxStyle.fillColor));
            FillOpacityBox().Value(box->boxStyle.fillOpacity * 100.0);
        }
        suppressFormatting_ = false;
    }

    void MainWindow::ApplyFormattingFromControls()
    {
        if (suppressFormatting_ || !session_ || !session_->selectedId()) return;
        auto* current = session_->findTextBox(*session_->selectedId());
        if (!current) return;
        auto updated = *current;
        updated.textStyle.family = static_cast<core::FontFamily>(std::max(0, FontCombo().SelectedIndex()));
        if (!std::isnan(FontSizeBox().Value())) updated.textStyle.sizePoints = FontSizeBox().Value();
        const auto bold = BoldButton().IsChecked();
        const auto italic = ItalicButton().IsChecked();
        updated.textStyle.bold = bold && bold.Value();
        updated.textStyle.italic = italic && italic.Value();
        updated.textStyle.alignment = static_cast<core::TextAlignment>(std::max(0, AlignmentCombo().SelectedIndex()));
        updated.textStyle.color = SelectedColor(TextColorCombo(), updated.textStyle.color);
        updated.boxStyle.borderColor = SelectedColor(BorderColorCombo(), updated.boxStyle.borderColor);
        if (!std::isnan(BorderWidthBox().Value())) updated.boxStyle.borderWidthPoints = BorderWidthBox().Value();
        updated.boxStyle.fillColor = SelectedColor(FillColorCombo(), updated.boxStyle.fillColor);
        if (!std::isnan(FillOpacityBox().Value())) updated.boxStyle.fillOpacity = FillOpacityBox().Value() / 100.0;
        const auto id = updated.id;
        const auto pageIndex = updated.pageIndex;
        session_->updateTextBox(updated);
        RebuildOverlay(pageIndex);
        ValidateBoxAsync(id);
        RefreshCommandState();
    }

    void MainWindow::OnSelectionFormattingChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        ApplyFormattingFromControls();
    }

    void MainWindow::OnToggleFormattingChanged(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyFormattingFromControls();
    }

    void MainWindow::OnNumberFormattingChanged(NumberBox const&, NumberBoxValueChangedEventArgs const&)
    {
        ApplyFormattingFromControls();
    }

    void MainWindow::RefreshCommandState()
    {
        const bool hasDocument = session_ != nullptr;
        const bool canSave = hasDocument && !session_->hasOverflow();
        SaveAsButton().IsEnabled(canSave);
        SaveButton().IsEnabled(canSave && (session_->dirty() || session_->outputPath().has_value()));
        UndoButton().IsEnabled(hasDocument && session_->canUndo());
        RedoButton().IsEnabled(hasDocument && session_->canRedo());
    }

    void MainWindow::OnUndo(IInspectable const&, RoutedEventArgs const&)
    {
        if (session_ && session_->undo()) RebuildAllOverlays();
    }

    void MainWindow::OnRedo(IInspectable const&, RoutedEventArgs const&)
    {
        if (session_ && session_->redo()) RebuildAllOverlays();
    }

    void MainWindow::OnSelectTool(IInspectable const&, RoutedEventArgs const&)
    {
        textTool_ = false;
        SelectToolButton().IsChecked(true);
        TextToolButton().IsChecked(false);
        StatusText().Text(L"Select tool — click a textbox to edit it.");
    }

    void MainWindow::OnTextTool(IInspectable const&, RoutedEventArgs const&)
    {
        textTool_ = true;
        SelectToolButton().IsChecked(false);
        TextToolButton().IsChecked(true);
        StatusText().Text(L"Text tool — drag a rectangle on a page.");
    }

    void MainWindow::OnToggleThumbnails(IInspectable const&, RoutedEventArgs const&)
    {
        const auto checked = PagesButton().IsChecked();
        const bool visible = checked && checked.Value();
        ThumbnailColumn().Width(GridLength{ visible ? 176.0 : 0.0, GridUnitType::Pixel });
        ThumbnailDividerColumn().Width(GridLength{ visible ? 4.0 : 0.0, GridUnitType::Pixel });
        ThumbnailRail().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        ThumbnailDivider().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::SetZoom(double percent)
    {
        ++renderGeneration_;
        for (auto& page : pageViews_)
        {
            page.fullRenderScale = 0.0;
            page.fullRendering = false;
            page.thumbnailRendering = false;
        }
        zoomPercent_ = std::clamp(percent, 25.0, 400.0);
        UpdatePageSizes();
        StatusText().Text(L"Zoom " + to_hstring(static_cast<int>(std::round(zoomPercent_))) + L"%");
    }

    fire_and_forget MainWindow::OnZoomChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto lifetime = get_strong();
        if (!initialized_ || !session_ || ZoomCombo().SelectedIndex() < 0) co_return;
        renderCancellation_.cancel();
        renderCancellation_ = core::CancellationToken{};
        const int index = ZoomCombo().SelectedIndex();
        if (index == 0)
        {
            fitWidth_ = true;
            fitPage_ = false;
            double widest = 1.0;
            for (auto const& page : session_->pages()) widest = std::max(widest, core::geometry::RotatedSize(page).width);
            SetZoom((std::max(120.0, DocumentScroll().ActualWidth() - 96.0) / widest) / (96.0 / 72.0) * 100.0);
        }
        else if (index == 1)
        {
            fitWidth_ = false;
            fitPage_ = true;
            const auto page = core::geometry::RotatedSize(session_->pages().front());
            const double byWidth = std::max(120.0, DocumentScroll().ActualWidth() - 96.0) / page.width;
            const double byHeight = std::max(120.0, DocumentScroll().ActualHeight() - 64.0) / page.height;
            SetZoom(std::min(byWidth, byHeight) / (96.0 / 72.0) * 100.0);
        }
        else
        {
            fitWidth_ = false;
            fitPage_ = false;
            auto item = ZoomCombo().SelectedItem().as<ComboBoxItem>();
            SetZoom(std::stod(unbox_value<hstring>(item.Tag()).c_str()));
        }
        try { co_await RenderAllAsync(); }
        catch (std::exception const& error) { co_await ShowMessageAsync(L"Rendering failed", to_hstring(error.what()).c_str()); }
    }

    fire_and_forget MainWindow::OnDocumentPointerWheelChanged(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        auto lifetime = get_strong();
        if (!session_ || !KeyDown(VK_CONTROL)) co_return;
        const int delta = args.GetCurrentPoint(DocumentScroll()).Properties().MouseWheelDelta();
        renderCancellation_.cancel();
        renderCancellation_ = core::CancellationToken{};
        SetZoom(zoomPercent_ + (delta > 0 ? 10.0 : -10.0));
        args.Handled(true);
        try { co_await RenderAllAsync(); }
        catch (std::exception const& error) { co_await ShowMessageAsync(L"Rendering failed", to_hstring(error.what()).c_str()); }
    }

    void MainWindow::OnThumbnailSelected(IInspectable const&, SelectionChangedEventArgs const&)
    {
        const int index = ThumbnailList().SelectedIndex();
        if (index < 0 || index >= static_cast<int>(pageViews_.size())) return;
        if (!synchronizingThumbnail_)
        {
            pageViews_[static_cast<std::size_t>(index)].shell.StartBringIntoView();
        }
        PageStatusText().Text(L"Page " + to_hstring(index + 1) + L" of " + to_hstring(pageViews_.size()));
    }

    void MainWindow::OnDocumentViewChanged(IInspectable const&, ScrollViewerViewChangedEventArgs const&)
    {
        if (pageViews_.empty()) return;
        std::size_t nearest = 0;
        double nearestDistance = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index < pageViews_.size(); ++index)
        {
            const auto transform = pageViews_[index].shell.TransformToVisual(DocumentScroll());
            const auto top = transform.TransformPoint(Point{ 0, 0 }).Y;
            const double distance = std::abs(static_cast<double>(top) - 24.0);
            if (distance < nearestDistance)
            {
                nearest = index;
                nearestDistance = distance;
            }
        }
        if (ThumbnailList().SelectedIndex() != static_cast<int>(nearest))
        {
            synchronizingThumbnail_ = true;
            ThumbnailList().SelectedIndex(static_cast<int>(nearest));
            synchronizingThumbnail_ = false;
        }
        PageStatusText().Text(L"Page " + to_hstring(nearest + 1) + L" of " + to_hstring(pageViews_.size()));
        RenderVisiblePagesAsync();
    }

    void MainWindow::DeleteSelection()
    {
        if (!session_ || !session_->selectedId()) return;
        const auto id = *session_->selectedId();
        session_->removeTextBox(id);
        RebuildAllOverlays();
    }

    void MainWindow::NudgeSelection(double viewDx, double viewDy)
    {
        if (!session_ || !session_->selectedId()) return;
        session_->beginTransaction();
        MoveBox(*session_->selectedId(), viewDx * DipScale(), viewDy * DipScale());
        session_->commitTransaction();
        RebuildAllOverlays();
    }

    fire_and_forget MainWindow::OnKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        auto lifetime = get_strong();
        const bool control = KeyDown(VK_CONTROL);
        const bool shift = KeyDown(VK_SHIFT);
        const auto key = args.Key();
        const auto focused = FocusManager::GetFocusedElement(RootGrid().XamlRoot());
        const bool editingText = focused && focused.try_as<TextBox>();
        if (control && key == VirtualKey::O)
        {
            args.Handled(true);
            OnOpen(IInspectable{ nullptr }, RoutedEventArgs{ nullptr });
        }
        else if (control && key == VirtualKey::S)
        {
            args.Handled(true);
            try { co_await SavePdfAsync(shift); }
            catch (std::exception const& error) { co_await ShowMessageAsync(L"Could not save PDF", to_hstring(error.what()).c_str()); }
        }
        else if (control && key == VirtualKey::Z)
        {
            args.Handled(true);
            if (session_ && session_->undo()) RebuildAllOverlays();
        }
        else if (control && key == VirtualKey::Y)
        {
            args.Handled(true);
            if (session_ && session_->redo()) RebuildAllOverlays();
        }
        else if (key == VirtualKey::Delete && !editingText)
        {
            args.Handled(true);
            DeleteSelection();
        }
        else if (key == VirtualKey::Escape)
        {
            args.Handled(true);
            drag_ = {};
            if (session_)
            {
                session_->commitTransaction();
                session_->removeEmptyTextBoxes();
                session_->select(std::nullopt);
            }
            OnSelectTool(IInspectable{ nullptr }, RoutedEventArgs{ nullptr });
            RebuildAllOverlays();
        }
        else if (!editingText && (key == VirtualKey::Left || key == VirtualKey::Right
            || key == VirtualKey::Up || key == VirtualKey::Down))
        {
            args.Handled(true);
            const double amount = shift ? 10.0 : 1.0;
            NudgeSelection(key == VirtualKey::Left ? -amount : key == VirtualKey::Right ? amount : 0.0,
                           key == VirtualKey::Up ? -amount : key == VirtualKey::Down ? amount : 0.0);
        }
        co_return;
    }

    std::optional<std::size_t> MainWindow::SelectedPageIndex() const
    {
        if (!session_ || !session_->selectedId()) return std::nullopt;
        if (const auto* box = session_->findTextBox(*session_->selectedId())) return box->pageIndex;
        return std::nullopt;
    }

    IAsyncAction MainWindow::ShowMessageAsync(std::wstring title, std::wstring message)
    {
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(hstring(title)));
        dialog.Content(box_value(hstring(message)));
        dialog.CloseButtonText(L"OK");
        co_await dialog.ShowAsync();
    }
}
