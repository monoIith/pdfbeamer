#include "PdfEditor/Core/DocumentSession.h"

#include "PdfEditor/Core/Geometry.h"

#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace pdfeditor::core
{
    DocumentSession::DocumentSession(std::filesystem::path sourcePath,
                                     std::vector<PageGeometry> pages,
                                     bool hasDigitalSignatures)
        : sourcePath_(std::move(sourcePath)),
          pages_(std::move(pages)),
          hasDigitalSignatures_(hasDigitalSignatures)
    {
        if (sourcePath_.empty())
        {
            throw std::invalid_argument("The source PDF path cannot be empty.");
        }
        if (pages_.empty())
        {
            throw std::invalid_argument("A PDF document must have at least one page.");
        }
    }

    bool DocumentSession::dirty() const noexcept
    {
        return state_.boxes != savedBoxes_;
    }

    bool DocumentSession::hasOverflow() const noexcept
    {
        return std::any_of(state_.boxes.begin(), state_.boxes.end(), [](const auto& box)
        {
            return !box.empty() && box.overflow;
        });
    }

    TextBoxModel& DocumentSession::addTextBox(std::size_t pageIndex, Rect bounds)
    {
        validatePageIndex(pageIndex);
        const auto id = CreateId();
        mutate([&]
        {
            TextBoxModel box;
            box.id = id;
            box.pageIndex = pageIndex;
            box.bounds = geometry::ClampToCropBox(pages_[pageIndex], bounds);
            state_.boxes.push_back(std::move(box));
            state_.selectedId = id;
        });
        return state_.boxes.back();
    }

    bool DocumentSession::updateTextBox(const TextBoxModel& box)
    {
        validatePageIndex(box.pageIndex);
        auto found = std::find_if(state_.boxes.begin(), state_.boxes.end(), [&](const auto& value)
        {
            return value.id == box.id;
        });
        if (found == state_.boxes.end())
        {
            return false;
        }

        auto updated = box;
        updated.bounds = geometry::ClampToCropBox(pages_[updated.pageIndex], updated.bounds);
        mutate([&] { *found = std::move(updated); });
        return true;
    }

    bool DocumentSession::setOverflow(std::string_view id, bool overflow) noexcept
    {
        auto* box = findTextBox(id);
        if (!box || box->overflow == overflow)
        {
            return box != nullptr;
        }
        // Overflow is derived layout state, not a user edit and therefore not an undo step.
        box->overflow = overflow;
        return true;
    }

    bool DocumentSession::removeTextBox(std::string_view id)
    {
        const auto found = std::find_if(state_.boxes.begin(), state_.boxes.end(), [&](const auto& value)
        {
            return value.id == id;
        });
        if (found == state_.boxes.end())
        {
            return false;
        }
        mutate([&]
        {
            state_.boxes.erase(found);
            if (state_.selectedId && *state_.selectedId == id)
            {
                state_.selectedId.reset();
            }
        });
        return true;
    }

    void DocumentSession::removeEmptyTextBoxes()
    {
        mutate([&]
        {
            state_.boxes.erase(std::remove_if(state_.boxes.begin(), state_.boxes.end(), [](const auto& box)
            {
                return box.empty();
            }), state_.boxes.end());
            if (state_.selectedId && !findTextBox(*state_.selectedId))
            {
                state_.selectedId.reset();
            }
        });
    }

    TextBoxModel* DocumentSession::findTextBox(std::string_view id) noexcept
    {
        const auto found = std::find_if(state_.boxes.begin(), state_.boxes.end(), [&](const auto& box)
        {
            return box.id == id;
        });
        return found == state_.boxes.end() ? nullptr : &*found;
    }

    const TextBoxModel* DocumentSession::findTextBox(std::string_view id) const noexcept
    {
        const auto found = std::find_if(state_.boxes.begin(), state_.boxes.end(), [&](const auto& box)
        {
            return box.id == id;
        });
        return found == state_.boxes.end() ? nullptr : &*found;
    }

    void DocumentSession::select(std::optional<std::string> id)
    {
        if (id && !findTextBox(*id))
        {
            throw std::invalid_argument("Cannot select an unknown textbox.");
        }
        state_.selectedId = std::move(id);
    }

    void DocumentSession::beginTransaction()
    {
        if (!transactionBefore_)
        {
            transactionBefore_ = state_;
        }
    }

    void DocumentSession::commitTransaction()
    {
        if (!transactionBefore_)
        {
            return;
        }
        if (*transactionBefore_ != state_)
        {
            undo_.push_back({ *transactionBefore_, state_ });
            redo_.clear();
        }
        transactionBefore_.reset();
    }

    void DocumentSession::cancelTransaction()
    {
        if (transactionBefore_)
        {
            state_ = std::move(*transactionBefore_);
            transactionBefore_.reset();
        }
    }

    bool DocumentSession::undo()
    {
        commitTransaction();
        if (undo_.empty())
        {
            return false;
        }
        auto change = std::move(undo_.back());
        undo_.pop_back();
        state_ = change.before;
        redo_.push_back(std::move(change));
        return true;
    }

    bool DocumentSession::redo()
    {
        commitTransaction();
        if (redo_.empty())
        {
            return false;
        }
        auto change = std::move(redo_.back());
        redo_.pop_back();
        state_ = change.after;
        undo_.push_back(std::move(change));
        return true;
    }

    void DocumentSession::markSaved(std::filesystem::path outputPath)
    {
        outputPath_ = std::move(outputPath);
        savedBoxes_ = state_.boxes;
    }

    std::string DocumentSession::CreateId()
    {
        static thread_local std::mt19937_64 generator{ std::random_device{}() };
        static constexpr char hex[] = "0123456789abcdef";
        std::array<unsigned char, 16> bytes{};
        for (auto& byte : bytes)
        {
            byte = static_cast<unsigned char>(generator() & 0xff);
        }
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

        std::string id;
        id.reserve(36);
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (index == 4 || index == 6 || index == 8 || index == 10) id.push_back('-');
            id.push_back(hex[bytes[index] >> 4]);
            id.push_back(hex[bytes[index] & 0x0f]);
        }
        return id;
    }

    void DocumentSession::validatePageIndex(std::size_t pageIndex) const
    {
        if (pageIndex >= pages_.size())
        {
            throw std::out_of_range("Textbox page index is outside the document.");
        }
    }
}
