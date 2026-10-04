#include "PdfEditor/Core/MuPdfBackend.h"

#include "PdfEditor/Core/Geometry.h"
#include "PdfEditor/Core/TextMarkup.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pdfeditor::core
{
    namespace
    {
        std::string PathToUtf8(const std::filesystem::path& path)
        {
#ifdef _WIN32
            const auto& wide = path.native();
            if (wide.empty()) return {};
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                                   nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                result.data(), bytes, nullptr, nullptr);
            return result;
#else
            return path.string();
#endif
        }

        bool HasPdfMagic(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::array<char, 5> magic{};
            stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            return stream.gcount() == static_cast<std::streamsize>(magic.size())
                && std::string_view(magic.data(), magic.size()) == "%PDF-";
        }

        std::filesystem::path NormalizedAbsolute(const std::filesystem::path& path)
        {
            std::error_code error;
            auto absolute = std::filesystem::absolute(path, error);
            if (error) absolute = path;
            return absolute.lexically_normal();
        }

        std::filesystem::path TemporarySibling(const std::filesystem::path& destination)
        {
            static thread_local std::mt19937_64 generator{ std::random_device{}() };
            auto temporary = destination;
#ifdef _WIN32
            temporary += L".tmp-" + std::to_wstring(generator());
#else
            temporary += ".tmp-" + std::to_string(generator());
#endif
            return temporary;
        }

        void AtomicInstall(const std::filesystem::path& temporary, const std::filesystem::path& destination)
        {
#ifdef _WIN32
            BOOL success = FALSE;
            if (std::filesystem::exists(destination))
            {
                success = ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                                       REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
            }
            else
            {
                success = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
            }
            if (!success)
            {
                throw PdfException("Could not install the saved PDF (Windows error "
                                   + std::to_string(GetLastError()) + ").");
            }
#else
            std::error_code error;
            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                throw PdfException("Could not install the saved PDF: " + error.message());
            }
#endif
        }

        std::string FontFaceCss()
        {
            return
                "@font-face{font-family:'Noto Sans PDE';src:url('NotoSans-Regular.ttf');font-weight:400;font-style:normal;}"
                "@font-face{font-family:'Noto Sans PDE';src:url('NotoSans-Bold.ttf');font-weight:700;font-style:normal;}"
                "@font-face{font-family:'Noto Sans PDE';src:url('NotoSans-Italic.ttf');font-weight:400;font-style:italic;}"
                "@font-face{font-family:'Noto Sans PDE';src:url('NotoSans-BoldItalic.ttf');font-weight:700;font-style:italic;}"
                "@font-face{font-family:'Noto Serif PDE';src:url('NotoSerif-Regular.ttf');font-weight:400;font-style:normal;}"
                "@font-face{font-family:'Noto Serif PDE';src:url('NotoSerif-Bold.ttf');font-weight:700;font-style:normal;}"
                "@font-face{font-family:'Noto Serif PDE';src:url('NotoSerif-Italic.ttf');font-weight:400;font-style:italic;}"
                "@font-face{font-family:'Noto Serif PDE';src:url('NotoSerif-BoldItalic.ttf');font-weight:700;font-style:italic;}"
                "@font-face{font-family:'Noto Sans Mono PDE';src:url('NotoSansMono-Regular.ttf');font-weight:400;font-style:normal;}"
                "@font-face{font-family:'Noto Sans Mono PDE';src:url('NotoSansMono-Bold.ttf');font-weight:700;font-style:normal;}";
        }

        fz_rect ToFzRect(const Rect& rect)
        {
            return fz_make_rect(static_cast<float>(rect.x), static_cast<float>(rect.y),
                                static_cast<float>(rect.right()), static_cast<float>(rect.bottom()));
        }

        Rect FromFzRect(const fz_rect& rect)
        {
            return { rect.x0, rect.y0, rect.x1 - rect.x0, rect.y1 - rect.y0 };
        }

        std::array<float, 3> ToRgb(const Color& color)
        {
            return {
                static_cast<float>(color.red) / 255.0f,
                static_cast<float>(color.green) / 255.0f,
                static_cast<float>(color.blue) / 255.0f,
            };
        }

        Rect TextArea(const TextBoxModel& box, const PageGeometry& page)
        {
            auto view = geometry::ToView(page, box.bounds);
            const double inset = box.boxStyle.paddingPoints + box.boxStyle.borderWidthPoints * 0.5;
            view.x += inset;
            view.y += inset;
            view.width = std::max(0.0, view.width - inset * 2.0);
            view.height = std::max(0.0, view.height - inset * 2.0);
            return view;
        }

        LayoutResult LayoutStory(fz_context* context,
                                 fz_archive* fontArchive,
                                 const TextBoxModel& box,
                                 const PageGeometry& page,
                                 fz_device* device)
        {
            if (box.empty())
            {
                return { true, {}, {} };
            }

            const auto area = TextArea(box, page);
            if (area.empty())
            {
                return { false, {}, "The textbox is too small for its padding and border." };
            }

            const auto html = markup::BuildStoryHtml(box);
            const auto css = FontFaceCss() + markup::BuildStoryCss(box);
            fz_buffer* buffer = nullptr;
            fz_story* story = nullptr;
            fz_rect filled = fz_empty_rect;
            int placement = 1;
            std::string error;

            fz_var(buffer);
            fz_var(story);
            fz_try(context)
            {
                buffer = fz_new_buffer_from_copied_data(context,
                    reinterpret_cast<const unsigned char*>(html.c_str()), html.size() + 1);
                story = fz_new_story(context, buffer, css.c_str(),
                                     static_cast<float>(box.textStyle.sizePoints), fontArchive);
                placement = fz_place_story_flags(context, story, ToFzRect(area), &filled,
                                                 FZ_PLACE_STORY_FLAG_NO_OVERFLOW);
                if (placement == FZ_PLACE_STORY_RETURN_ALL_FITTED && device)
                {
                    fz_draw_story(context, story, device, fz_identity);
                }
            }
            fz_always(context)
            {
                fz_drop_story(context, story);
                fz_drop_buffer(context, buffer);
            }
            fz_catch(context)
            {
                error = fz_caught_message(context);
            }

            if (!error.empty())
            {
                throw PdfException("Text layout failed: " + error);
            }
            if (placement != FZ_PLACE_STORY_RETURN_ALL_FITTED)
            {
                return { false, FromFzRect(filled),
                         placement == FZ_PLACE_STORY_RETURN_OVERFLOW_WIDTH
                             ? "A word is wider than the textbox."
                             : "The text is taller than the textbox." };
            }
            return { true, FromFzRect(filled), {} };
        }

        void DrawBox(fz_context* context,
                     fz_archive* fontArchive,
                     fz_device* device,
                     const TextBoxModel& box,
                     const PageGeometry& page,
                     bool requireFit = true)
        {
            const auto view = geometry::ToView(page, box.bounds);
            const auto textArea = TextArea(box, page);
            const auto html = markup::BuildStoryHtml(box);
            const auto css = FontFaceCss() + markup::BuildStoryCss(box);
            fz_path* path = nullptr;
            fz_path* clip = nullptr;
            fz_buffer* buffer = nullptr;
            fz_story* story = nullptr;
            std::string error;
            fz_var(path);
            fz_var(clip);
            fz_var(buffer);
            fz_var(story);
            fz_try(context)
            {
                path = fz_new_path(context);
                fz_rectto(context, path, static_cast<float>(view.x), static_cast<float>(view.y),
                          static_cast<float>(view.right()), static_cast<float>(view.bottom()));

                if (box.boxStyle.fillOpacity > 0.0)
                {
                    const auto rgb = ToRgb(box.boxStyle.fillColor);
                    fz_fill_path(context, device, path, 0, fz_identity, fz_device_rgb(context), rgb.data(),
                                 static_cast<float>(std::clamp(box.boxStyle.fillOpacity, 0.0, 1.0)),
                                 fz_default_color_params);
                }
                if (box.boxStyle.borderWidthPoints > 0.0)
                {
                    auto stroke = fz_default_stroke_state;
                    stroke.linewidth = static_cast<float>(box.boxStyle.borderWidthPoints);
                    const auto rgb = ToRgb(box.boxStyle.borderColor);
                    fz_stroke_path(context, device, path, &stroke, fz_identity, fz_device_rgb(context),
                                   rgb.data(), 1.0f, fz_default_color_params);
                }

                if (!box.empty())
                {
                    if (textArea.empty())
                    {
                        fz_throw(context, FZ_ERROR_LIMIT, "textbox is too small for its padding and border");
                    }
                    clip = fz_new_path(context);
                    fz_rectto(context, clip, static_cast<float>(textArea.x), static_cast<float>(textArea.y),
                              static_cast<float>(textArea.right()), static_cast<float>(textArea.bottom()));
                    fz_clip_path(context, device, clip, 0, fz_identity, ToFzRect(textArea));

                    buffer = fz_new_buffer_from_copied_data(context,
                        reinterpret_cast<const unsigned char*>(html.c_str()), html.size() + 1);
                    story = fz_new_story(context, buffer, css.c_str(),
                                         static_cast<float>(box.textStyle.sizePoints), fontArchive);
                    fz_rect filled = fz_empty_rect;
                    const int flags = requireFit ? FZ_PLACE_STORY_FLAG_NO_OVERFLOW : 0;
                    const int placement = fz_place_story_flags(context, story, ToFzRect(textArea), &filled, flags);
                    if (requireFit && placement != FZ_PLACE_STORY_RETURN_ALL_FITTED)
                    {
                        fz_throw(context, FZ_ERROR_LIMIT, "textbox text overflowed while saving");
                    }
                    fz_draw_story(context, story, device, fz_identity);
                    fz_pop_clip(context, device);
                }
            }
            fz_always(context)
            {
                fz_drop_story(context, story);
                fz_drop_buffer(context, buffer);
                fz_drop_path(context, clip);
                fz_drop_path(context, path);
            }
            fz_catch(context)
            {
                error = fz_caught_message(context);
            }
            if (!error.empty())
            {
                throw PdfException("Could not draw a textbox: " + error);
            }
        }

        void AddOverlayFormToPage(fz_context* context,
                                  pdf_document* document,
                                  std::size_t pageIndex,
                                  fz_rect fitzPageBox,
                                  pdf_obj* formResources,
                                  fz_buffer* formContents)
        {
            pdf_obj* pageObject = pdf_lookup_page_obj(context, document, static_cast<int>(pageIndex));
            // pdf_page_transform returns the crop box in PDF user space and a matrix
            // that maps crop-local Fitz coordinates into that space. The PDF device
            // has already applied the matrix to the generated stream, so the form BBox
            // must remain the untransformed crop box.
            const fz_rect pdfBounds = fitzPageBox;
            pdf_obj* form = nullptr;
            pdf_obj* pageResources = nullptr;
            pdf_obj* xObjects = nullptr;
            fz_buffer* invocation = nullptr;
            pdf_obj* stream = nullptr;
            pdf_obj* newContents = nullptr;
            fz_var(form);
            fz_var(pageResources);
            fz_var(xObjects);
            fz_var(invocation);
            fz_var(stream);
            fz_var(newContents);

            fz_try(context)
            {
                form = pdf_new_xobject(context, document, pdfBounds, fz_identity,
                                       formResources, formContents);
                const pdf_obj* inherited = pdf_dict_get_inheritable(context, pageObject, PDF_NAME(Resources));
                pageResources = inherited
                    ? pdf_copy_dict(context, const_cast<pdf_obj*>(inherited))
                    : pdf_new_dict(context, document, 4);
                pdf_dict_put(context, pageObject, PDF_NAME(Resources), pageResources);

                const pdf_obj* existingXObjects = pdf_dict_get(context, pageResources, PDF_NAME(XObject));
                xObjects = existingXObjects
                    ? pdf_copy_dict(context, const_cast<pdf_obj*>(existingXObjects))
                    : pdf_new_dict(context, document, 4);
                pdf_dict_put(context, pageResources, PDF_NAME(XObject), xObjects);

                std::string name = "PDEditOverlay";
                int suffix = 1;
                while (pdf_dict_gets(context, xObjects, name.c_str()))
                {
                    name = "PDEditOverlay" + std::to_string(suffix++);
                }
                pdf_dict_puts(context, xObjects, name.c_str(), form);

                invocation = fz_new_buffer(context, 64);
                fz_append_printf(context, invocation, "q /%s Do Q\n", name.c_str());
                stream = pdf_add_stream(context, document, invocation, nullptr, 0);

                pdf_obj* contents = pdf_dict_get(context, pageObject, PDF_NAME(Contents));
                if (pdf_is_array(context, contents))
                {
                    pdf_array_push(context, contents, stream);
                }
                else
                {
                    newContents = pdf_new_array(context, document, 2);
                    if (contents) pdf_array_push(context, newContents, contents);
                    pdf_array_push(context, newContents, stream);
                    pdf_dict_put(context, pageObject, PDF_NAME(Contents), newContents);
                }
            }
            fz_always(context)
            {
                pdf_drop_obj(context, newContents);
                pdf_drop_obj(context, stream);
                fz_drop_buffer(context, invocation);
                pdf_drop_obj(context, xObjects);
                pdf_drop_obj(context, pageResources);
                pdf_drop_obj(context, form);
            }
            fz_catch(context)
            {
                fz_rethrow(context);
            }
        }
    }

    struct MuPdfBackend::Impl
    {
        explicit Impl(std::filesystem::path fonts)
            : fontDirectory(std::move(fonts))
        {
            static constexpr std::array<const char*, 10> requiredFonts{
                "NotoSans-Regular.ttf", "NotoSans-Bold.ttf", "NotoSans-Italic.ttf", "NotoSans-BoldItalic.ttf",
                "NotoSerif-Regular.ttf", "NotoSerif-Bold.ttf", "NotoSerif-Italic.ttf", "NotoSerif-BoldItalic.ttf",
                "NotoSansMono-Regular.ttf", "NotoSansMono-Bold.ttf",
            };
            for (const auto* font : requiredFonts)
            {
                if (!std::filesystem::exists(fontDirectory / font))
                {
                    throw PdfException(std::string("Required bundled font is missing: ") + font);
                }
            }

            context = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
            if (!context)
            {
                throw PdfException("MuPDF could not create its runtime context.");
            }
            std::string error;
            fz_try(context)
            {
                fz_register_document_handlers(context);
                const auto path = PathToUtf8(fontDirectory);
                fontArchive = fz_open_directory(context, path.c_str());
            }
            fz_catch(context)
            {
                error = fz_caught_message(context);
            }
            if (!error.empty())
            {
                fz_drop_context(context);
                context = nullptr;
                throw PdfException("Could not open the bundled font directory: " + error);
            }
        }

        ~Impl()
        {
            if (!context) return;
            pdf_drop_document(context, document);
            fz_drop_archive(context, fontArchive);
            fz_drop_context(context);
        }

        fz_context* context{};
        pdf_document* document{};
        fz_archive* fontArchive{};
        std::filesystem::path fontDirectory;
        std::filesystem::path openPath;
        std::vector<PageGeometry> pages;
    };

    MuPdfBackend::MuPdfBackend(std::filesystem::path fontDirectory)
        : impl_(std::make_unique<Impl>(std::move(fontDirectory)))
    {
    }

    MuPdfBackend::~MuPdfBackend() = default;

    OpenDocumentResult MuPdfBackend::open(const std::filesystem::path& path)
    {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".pdf" || !HasPdfMagic(path))
        {
            throw PdfException("The selected file is not a PDF document.");
        }

        pdf_document* opened = nullptr;
        fz_stream* stream = nullptr;
        std::vector<PageGeometry> pages;
        bool encrypted = false;
        bool signedDocument = false;
        bool repaired = false;
        std::string error;
        fz_var(opened);
        fz_var(stream);

        fz_try(impl_->context)
        {
#ifdef _WIN32
            stream = fz_open_file_w(impl_->context, path.c_str());
#else
            const auto utf8 = PathToUtf8(path);
            stream = fz_open_file(impl_->context, utf8.c_str());
#endif
            opened = pdf_open_document_with_stream(impl_->context, stream);
            pdf_disable_js(impl_->context, opened);
            encrypted = pdf_needs_password(impl_->context, opened) != 0;
            if (!encrypted)
            {
                pdf_check_document(impl_->context, opened);
                repaired = pdf_was_repaired(impl_->context, opened) != 0;
                signedDocument = pdf_count_signatures(impl_->context, opened) > 0;
                const int count = pdf_count_pages(impl_->context, opened);
                pages.reserve(static_cast<std::size_t>(count));
                for (int index = 0; index < count; ++index)
                {
                    pdf_obj* page = pdf_lookup_page_obj(impl_->context, opened, index);
                    fz_rect media = pdf_dict_get_inheritable_rect(impl_->context, page, PDF_NAME(MediaBox));
                    fz_rect crop = pdf_dict_get_inheritable_rect(impl_->context, page, PDF_NAME(CropBox));
                    if (fz_is_empty_rect(crop)) crop = media;
                    const int rotation = geometry::NormalizeRotation(
                        pdf_dict_get_inheritable_int(impl_->context, page, PDF_NAME(Rotate)));
                    pages.push_back({
                        { 0, 0, std::abs(media.x1 - media.x0), std::abs(media.y1 - media.y0) },
                        { 0, 0, std::abs(crop.x1 - crop.x0), std::abs(crop.y1 - crop.y0) },
                        rotation,
                    });
                }
            }
        }
        fz_always(impl_->context)
        {
            fz_drop_stream(impl_->context, stream);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }

        if (!error.empty())
        {
            pdf_drop_document(impl_->context, opened);
            throw PdfException("Could not open the PDF: " + error);
        }
        if (encrypted)
        {
            pdf_drop_document(impl_->context, opened);
            throw PdfException("Password-protected PDFs are not supported in this milestone.");
        }
        if (repaired)
        {
            pdf_drop_document(impl_->context, opened);
            throw PdfException("The PDF is malformed and required structural repair. Open a repaired copy instead.");
        }
        if (pages.empty())
        {
            pdf_drop_document(impl_->context, opened);
            throw PdfException("The PDF has no pages.");
        }

        pdf_drop_document(impl_->context, impl_->document);
        impl_->document = opened;
        impl_->openPath = path;
        impl_->pages = pages;
        return { std::move(pages), signedDocument, repaired };
    }

    PixelBuffer MuPdfBackend::renderPage(std::size_t pageIndex,
                                         double pixelsPerPoint,
                                         const CancellationToken& cancellation)
    {
        if (!impl_->document || pageIndex >= impl_->pages.size())
        {
            throw PdfException("The requested page is not open.");
        }
        if (pixelsPerPoint <= 0.0)
        {
            throw PdfException("The render scale must be positive.");
        }
        if (cancellation.cancelled()) return {};

        pdf_page* page = nullptr;
        fz_pixmap* pixmap = nullptr;
        PixelBuffer result;
        std::string error;
        fz_var(page);
        fz_var(pixmap);
        fz_try(impl_->context)
        {
            page = pdf_load_page(impl_->context, impl_->document, static_cast<int>(pageIndex));
            const auto scale = fz_scale(static_cast<float>(pixelsPerPoint), static_cast<float>(pixelsPerPoint));
            pixmap = fz_new_pixmap_from_page(impl_->context, reinterpret_cast<fz_page*>(page),
                                             scale, fz_device_bgr(impl_->context), 0);
            result.width = fz_pixmap_width(impl_->context, pixmap);
            result.height = fz_pixmap_height(impl_->context, pixmap);
            result.stride = result.width * 4;
            result.pixels.resize(static_cast<std::size_t>(result.stride) * result.height);
            const int sourceStride = fz_pixmap_stride(impl_->context, pixmap);
            const auto* source = fz_pixmap_samples(impl_->context, pixmap);
            for (int y = 0; y < result.height; ++y)
            {
                const auto* sourceRow = source + static_cast<std::ptrdiff_t>(y) * sourceStride;
                auto* targetRow = result.pixels.data() + static_cast<std::ptrdiff_t>(y) * result.stride;
                for (int x = 0; x < result.width; ++x)
                {
                    targetRow[x * 4 + 0] = sourceRow[x * 3 + 0];
                    targetRow[x * 4 + 1] = sourceRow[x * 3 + 1];
                    targetRow[x * 4 + 2] = sourceRow[x * 3 + 2];
                    targetRow[x * 4 + 3] = 255;
                }
            }
        }
        fz_always(impl_->context)
        {
            fz_drop_pixmap(impl_->context, pixmap);
            pdf_drop_page(impl_->context, page);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }
        if (!error.empty())
        {
            throw PdfException("Could not render the PDF page: " + error);
        }
        if (cancellation.cancelled()) return {};
        return result;
    }

    PixelBuffer MuPdfBackend::renderTextBox(const TextBoxModel& box,
                                            const PageGeometry& page,
                                            double pixelsPerPoint,
                                            const CancellationToken& cancellation)
    {
        if (pixelsPerPoint <= 0.0)
        {
            throw PdfException("The render scale must be positive.");
        }
        if (cancellation.cancelled()) return {};

        const auto view = geometry::ToView(page, box.bounds);
        const int width = std::max(1, static_cast<int>(std::ceil(view.width * pixelsPerPoint)));
        const int height = std::max(1, static_cast<int>(std::ceil(view.height * pixelsPerPoint)));
        const auto transform = fz_post_scale(
            fz_translate(static_cast<float>(-view.x), static_cast<float>(-view.y)),
            static_cast<float>(pixelsPerPoint), static_cast<float>(pixelsPerPoint));

        fz_pixmap* pixmap = nullptr;
        fz_device* device = nullptr;
        std::string error;
        fz_var(pixmap);
        fz_var(device);
        fz_try(impl_->context)
        {
            pixmap = fz_new_pixmap_with_bbox(impl_->context, fz_device_bgr(impl_->context),
                                             fz_make_irect(0, 0, width, height), nullptr, 1);
            fz_clear_pixmap(impl_->context, pixmap);
            device = fz_new_draw_device(impl_->context, transform, pixmap);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }
        if (!error.empty())
        {
            fz_drop_device(impl_->context, device);
            fz_drop_pixmap(impl_->context, pixmap);
            throw PdfException("Could not create a textbox preview: " + error);
        }

        try
        {
            DrawBox(impl_->context, impl_->fontArchive, device, box, page, false);
        }
        catch (...)
        {
            fz_drop_device(impl_->context, device);
            fz_drop_pixmap(impl_->context, pixmap);
            throw;
        }

        fz_try(impl_->context)
        {
            fz_close_device(impl_->context, device);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }
        if (!error.empty())
        {
            fz_drop_device(impl_->context, device);
            fz_drop_pixmap(impl_->context, pixmap);
            throw PdfException("Could not finish a textbox preview: " + error);
        }

        PixelBuffer result;
        result.width = width;
        result.height = height;
        result.stride = width * 4;
        result.pixels.resize(static_cast<std::size_t>(result.stride) * height);
        if (fz_pixmap_components(impl_->context, pixmap) != 4 || !fz_pixmap_alpha(impl_->context, pixmap))
        {
            fz_drop_device(impl_->context, device);
            fz_drop_pixmap(impl_->context, pixmap);
            throw PdfException("MuPDF returned an unexpected textbox preview pixel format.");
        }
        const int sourceStride = fz_pixmap_stride(impl_->context, pixmap);
        const auto* source = fz_pixmap_samples(impl_->context, pixmap);
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(result.pixels.data() + static_cast<std::ptrdiff_t>(y) * result.stride,
                        source + static_cast<std::ptrdiff_t>(y) * sourceStride,
                        static_cast<std::size_t>(result.stride));
        }
        fz_drop_device(impl_->context, device);
        fz_drop_pixmap(impl_->context, pixmap);
        if (cancellation.cancelled()) return {};
        return result;
    }

    std::string MuPdfBackend::extractText(std::size_t pageIndex)
    {
        if (!impl_->document || pageIndex >= impl_->pages.size())
        {
            throw PdfException("The requested page is not open.");
        }

        pdf_page* page = nullptr;
        fz_buffer* buffer = nullptr;
        std::string result;
        std::string error;
        fz_var(page);
        fz_var(buffer);
        fz_try(impl_->context)
        {
            page = pdf_load_page(impl_->context, impl_->document, static_cast<int>(pageIndex));
            buffer = fz_new_buffer_from_page(impl_->context, reinterpret_cast<fz_page*>(page), nullptr);
            unsigned char* data = nullptr;
            const auto length = fz_buffer_storage(impl_->context, buffer, &data);
            result.assign(reinterpret_cast<const char*>(data), length);
        }
        fz_always(impl_->context)
        {
            fz_drop_buffer(impl_->context, buffer);
            pdf_drop_page(impl_->context, page);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }
        if (!error.empty())
        {
            throw PdfException("Could not extract page text: " + error);
        }
        return result;
    }

    LayoutResult MuPdfBackend::validateTextBox(const TextBoxModel& box, const PageGeometry& page)
    {
        return LayoutStory(impl_->context, impl_->fontArchive, box, page, nullptr);
    }

    SaveResult MuPdfBackend::saveAs(const std::filesystem::path& sourcePath,
                                    const std::filesystem::path& destinationPath,
                                    std::span<const PageGeometry> pages,
                                    std::span<const TextBoxModel> textBoxes,
                                    const CancellationToken& cancellation)
    {
        if (NormalizedAbsolute(sourcePath) == NormalizedAbsolute(destinationPath))
        {
            throw PdfException("Choose a different file name so the original PDF remains unchanged.");
        }
        auto extension = destinationPath.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".pdf")
        {
            throw PdfException("The output file must use the .pdf extension.");
        }
        for (const auto& box : textBoxes)
        {
            if (box.pageIndex >= pages.size())
            {
                throw PdfException("A textbox refers to a page that is not in the document.");
            }
            if (!box.empty() && !validateTextBox(box, pages[box.pageIndex]).fits)
            {
                throw PdfException("Resize overflowing textboxes before saving.");
            }
        }
        if (cancellation.cancelled()) return {};

        const auto temporary = TemporarySibling(destinationPath);
        pdf_document* output = nullptr;
        fz_stream* sourceStream = nullptr;
        fz_stream* verifyStream = nullptr;
        pdf_document* verifyDocument = nullptr;
        std::string error;
        fz_var(output);
        fz_var(sourceStream);
        fz_var(verifyStream);
        fz_var(verifyDocument);

        fz_try(impl_->context)
        {
#ifdef _WIN32
            sourceStream = fz_open_file_w(impl_->context, sourcePath.c_str());
#else
            const auto sourceUtf8 = PathToUtf8(sourcePath);
            sourceStream = fz_open_file(impl_->context, sourceUtf8.c_str());
#endif
            output = pdf_open_document_with_stream(impl_->context, sourceStream);
            pdf_disable_js(impl_->context, output);
            pdf_check_document(impl_->context, output);

            for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
            {
                if (cancellation.cancelled())
                {
                    fz_throw(impl_->context, FZ_ERROR_ABORT, "save cancelled");
                }
                const bool touched = std::any_of(textBoxes.begin(), textBoxes.end(), [&](const auto& box)
                {
                    return box.pageIndex == pageIndex && !box.empty();
                });
                if (!touched) continue;

                pdf_page* page = nullptr;
                pdf_obj* resources = nullptr;
                fz_buffer* contents = nullptr;
                fz_device* device = nullptr;
                fz_var(page);
                fz_var(resources);
                fz_var(contents);
                fz_var(device);
                fz_try(impl_->context)
                {
                    page = pdf_load_page(impl_->context, output, static_cast<int>(pageIndex));
                    fz_rect fitzBox{};
                    fz_matrix fitzToPdf{};
                    pdf_page_transform(impl_->context, page, &fitzBox, &fitzToPdf);
                    resources = pdf_new_dict(impl_->context, output, 8);
                    contents = fz_new_buffer(impl_->context, 2048);
                    device = pdf_new_pdf_device(impl_->context, output, fitzToPdf, resources, contents);

                    for (const auto& box : textBoxes)
                    {
                        if (box.pageIndex == pageIndex && !box.empty())
                        {
                            DrawBox(impl_->context, impl_->fontArchive, device, box, pages[pageIndex]);
                        }
                    }
                    fz_close_device(impl_->context, device);
                    AddOverlayFormToPage(impl_->context, output, pageIndex, fitzBox,
                                         resources, contents);
                }
                fz_always(impl_->context)
                {
                    fz_drop_device(impl_->context, device);
                    fz_drop_buffer(impl_->context, contents);
                    pdf_drop_obj(impl_->context, resources);
                    pdf_drop_page(impl_->context, page);
                }
                fz_catch(impl_->context)
                {
                    fz_rethrow(impl_->context);
                }
            }

            pdf_subset_fonts(impl_->context, output, 0, nullptr);
            auto writeOptions = pdf_default_write_options;
            writeOptions.do_compress = 1;
            writeOptions.do_compress_images = 1;
            writeOptions.do_compress_fonts = 1;
            writeOptions.do_garbage = 1;
            const auto temporaryUtf8 = PathToUtf8(temporary);
            pdf_save_document(impl_->context, output, temporaryUtf8.c_str(), &writeOptions);

#ifdef _WIN32
            verifyStream = fz_open_file_w(impl_->context, temporary.c_str());
#else
            verifyStream = fz_open_file(impl_->context, temporaryUtf8.c_str());
#endif
            verifyDocument = pdf_open_document_with_stream(impl_->context, verifyStream);
            if (pdf_count_pages(impl_->context, verifyDocument) != static_cast<int>(pages.size()))
            {
                fz_throw(impl_->context, FZ_ERROR_FORMAT, "saved PDF page count changed");
            }
        }
        fz_always(impl_->context)
        {
            pdf_drop_document(impl_->context, verifyDocument);
            fz_drop_stream(impl_->context, verifyStream);
            pdf_drop_document(impl_->context, output);
            fz_drop_stream(impl_->context, sourceStream);
        }
        fz_catch(impl_->context)
        {
            error = fz_caught_message(impl_->context);
        }

        if (!error.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (cancellation.cancelled()) return {};
            throw PdfException("Could not save the PDF: " + error);
        }

        try
        {
            AtomicInstall(temporary, destinationPath);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        return { destinationPath, std::filesystem::file_size(destinationPath) };
    }
}
