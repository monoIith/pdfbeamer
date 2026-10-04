#pragma once

#include "Models.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace pdfeditor::core
{
    class DocumentSession
    {
    public:
        DocumentSession(std::filesystem::path sourcePath,
                        std::vector<PageGeometry> pages,
                        bool hasDigitalSignatures = false);

        [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }
        [[nodiscard]] const std::optional<std::filesystem::path>& outputPath() const noexcept { return outputPath_; }
        [[nodiscard]] const std::vector<PageGeometry>& pages() const noexcept { return pages_; }
        [[nodiscard]] const std::vector<TextBoxModel>& textBoxes() const noexcept { return state_.boxes; }
        [[nodiscard]] const std::optional<std::string>& selectedId() const noexcept { return state_.selectedId; }
        [[nodiscard]] bool hasDigitalSignatures() const noexcept { return hasDigitalSignatures_; }
        [[nodiscard]] bool dirty() const noexcept;
        [[nodiscard]] bool hasOverflow() const noexcept;
        [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
        [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }

        TextBoxModel& addTextBox(std::size_t pageIndex, Rect bounds);
        bool updateTextBox(const TextBoxModel& box);
        bool setOverflow(std::string_view id, bool overflow) noexcept;
        bool removeTextBox(std::string_view id);
        void removeEmptyTextBoxes();
        [[nodiscard]] TextBoxModel* findTextBox(std::string_view id) noexcept;
        [[nodiscard]] const TextBoxModel* findTextBox(std::string_view id) const noexcept;
        void select(std::optional<std::string> id);

        void beginTransaction();
        void commitTransaction();
        void cancelTransaction();
        bool undo();
        bool redo();
        void markSaved(std::filesystem::path outputPath);

    private:
        struct State
        {
            std::vector<TextBoxModel> boxes;
            std::optional<std::string> selectedId;
            auto operator<=>(const State&) const = default;
        };

        struct Change
        {
            State before;
            State after;
        };

        template <typename Mutation>
        void mutate(Mutation&& mutation)
        {
            const auto before = state_;
            mutation();
            if (before == state_)
            {
                return;
            }
            if (!transactionBefore_)
            {
                undo_.push_back({ before, state_ });
                redo_.clear();
            }
        }

        static std::string CreateId();
        void validatePageIndex(std::size_t pageIndex) const;

        std::filesystem::path sourcePath_;
        std::optional<std::filesystem::path> outputPath_;
        std::vector<PageGeometry> pages_;
        bool hasDigitalSignatures_{};
        State state_;
        std::vector<TextBoxModel> savedBoxes_;
        std::vector<Change> undo_;
        std::vector<Change> redo_;
        std::optional<State> transactionBefore_;
    };
}
