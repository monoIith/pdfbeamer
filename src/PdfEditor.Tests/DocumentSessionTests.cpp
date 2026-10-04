#include "TestHarness.h"

#include "PdfEditor/Core/DocumentSession.h"

using namespace pdfeditor::core;

namespace
{
    DocumentSession Session()
    {
        return DocumentSession("input.pdf", { PageGeometry{ { 0, 0, 600, 800 }, { 0, 0, 600, 800 }, 0 } });
    }
}

TEST(DocumentSession, AddsUpdatesAndRemovesBoxes)
{
    auto session = Session();
    auto& added = session.addTextBox(0, { 10, 20, 200, 80 });
    const auto id = added.id;
    EXPECT_TRUE(session.dirty());
    EXPECT_EQ(session.textBoxes().size(), std::size_t{ 1 });

    auto updated = *session.findTextBox(id);
    updated.text = u"Hello";
    EXPECT_TRUE(session.updateTextBox(updated));
    EXPECT_EQ(session.findTextBox(id)->text, u"Hello");

    EXPECT_TRUE(session.removeTextBox(id));
    EXPECT_TRUE(session.textBoxes().empty());
}

TEST(DocumentSession, CoalescesATransactionIntoOneUndoStep)
{
    auto session = Session();
    const auto id = session.addTextBox(0, { 10, 20, 200, 80 }).id;
    session.beginTransaction();
    auto box = *session.findTextBox(id);
    box.text = u"H";
    session.updateTextBox(box);
    box.text = u"Hello";
    session.updateTextBox(box);
    session.commitTransaction();

    EXPECT_TRUE(session.undo());
    EXPECT_TRUE(session.findTextBox(id)->text.empty());
    EXPECT_TRUE(session.redo());
    EXPECT_EQ(session.findTextBox(id)->text, u"Hello");
}

TEST(DocumentSession, TracksSavedStateAndOverflow)
{
    auto session = Session();
    auto box = session.addTextBox(0, { 10, 20, 200, 80 });
    box.text = u"Too much text";
    box.overflow = true;
    session.updateTextBox(box);
    EXPECT_TRUE(session.hasOverflow());
    session.markSaved("output.pdf");
    EXPECT_FALSE(session.dirty());
    EXPECT_EQ(session.outputPath().value(), std::filesystem::path("output.pdf"));
}

TEST(DocumentSession, RejectsBoxesForUnknownPages)
{
    auto session = Session();
    EXPECT_THROW(session.addTextBox(1, { 0, 0, 10, 10 }), std::out_of_range);
}

