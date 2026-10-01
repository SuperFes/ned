#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <new>

#include "Editor/Acp/Manager.h"
#include "Editor/ChangeSignature.h"
#include "Editor/Command.h"
#include "Editor/Lsp/Content.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Markdown.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Editor/OrgCapture.h"
#include "Editor/Project/Agenda.h"
#include "Editor/Project/Search.h"
#include "Editor/Protocol/FramedConnection.h"
#include "Editor/Snippet.h"
#include "Editor/Vim/ExCommand.h"
#include "Editor/Vim/Types.h"
#include "Text/Buffer.h"
#include "Text/ConflictHunk.h"
#include "Text/UndoTree.h"
#include "UI/AcpPanel/TranscriptFormat.h"
#include "UI/BufferView/GutterModel.h"
#include "UI/BufferView/RenderTypes.h"
#include "UI/Layout.h"

// A field with no default member initializer is left holding whatever was in
// memory when its struct is default-initialized (`T t;`, make_unique_for_
// overwrite, a resize of raw storage). Each type below is built over garbage
// and every such field is checked against its value-initialized form.

namespace {

template <typename T>
class OverGarbage {
  public:
    OverGarbage() {
        storage_.fill(std::byte{0xA5});
        // Placement new is the only spelling that default-initializes into
        // storage we control; nothing is allocated.
        value_ = ::new (storage_.data()) T;
    }
    ~OverGarbage() {
        std::destroy_at(value_);
    }
    OverGarbage(const OverGarbage&)            = delete;
    OverGarbage& operator=(const OverGarbage&) = delete;

    const T* operator->() const {
        return value_;
    }

  private:
    alignas(T) std::array<std::byte, sizeof(T)> storage_;
    T* value_ = nullptr;
};

} // namespace

#define REQUIRE_DEFAULTED(Type, ...)          \
    do {                                      \
        const OverGarbage<Type> garbage;      \
        const Type              clean{};      \
        for (const bool same : __VA_ARGS__) { \
            REQUIRE(same);                    \
        }                                     \
    }                                         \
    while (false)

#define SAME(field) (garbage->field == clean.field)

TEST_CASE("Editor aggregates default-initialize every field", "[DefaultInit]") {
    using namespace ned::editor;
    REQUIRE_DEFAULTED(acp::Manager::TranscriptEntry, {SAME(kind)});
    REQUIRE_DEFAULTED(changesig::ParamOrigin, {SAME(kind)});
    REQUIRE_DEFAULTED(CommandContext::VisualRow, {SAME(start), SAME(end)});
    REQUIRE_DEFAULTED(lsp::SemanticToken, {SAME(length), SAME(tokenTypeIndex), SAME(tokenModifiers)});
    REQUIRE_DEFAULTED(markdown::Table, {SAME(startLine), SAME(endLine)});
    REQUIRE_DEFAULTED(HighlightSpan, {SAME(startByte), SAME(endByte), SAME(syntaxClass)});
    REQUIRE_DEFAULTED(SignatureParameter, {SAME(startByte), SAME(endByte)});
    REQUIRE_DEFAULTED(CallArgument, {SAME(startByte), SAME(endByte)});
    REQUIRE_DEFAULTED(org::CaptureTemplate, {SAME(key)});
    REQUIRE_DEFAULTED(AgendaItem, {SAME(section)});
    REQUIRE_DEFAULTED(SearchMatch, {SAME(lineNumber)});
    REQUIRE_DEFAULTED(protocol::FramedConnection<lsp::Transport>::Options, {SAME(logCategory)});
    REQUIRE_DEFAULTED(SnippetField, {SAME(index), SAME(start), SAME(end)});
    REQUIRE_DEFAULTED(vim::ExRange, {SAME(startLine), SAME(endLine)});
    REQUIRE_DEFAULTED(vim::MotionResult, {SAME(target)});
    REQUIRE_DEFAULTED(vim::ObjectRange, {SAME(start), SAME(end)});
}

TEST_CASE("Org aggregates default-initialize every field", "[DefaultInit]") {
    using namespace ned::editor::org;
    REQUIRE_DEFAULTED(Headline, {SAME(level), SAME(lineNumber), SAME(lineStartByte), SAME(lineEndByte), SAME(tagsStartByte)});
    REQUIRE_DEFAULTED(Checkbox, {SAME(indent), SAME(state), SAME(lineNumber), SAME(stateByte)});
    REQUIRE_DEFAULTED(OrgTable, {SAME(startLine), SAME(endLine)});
    REQUIRE_DEFAULTED(OrgTimestamp, {SAME(date)});
    REQUIRE_DEFAULTED(Property, {SAME(lineStartByte), SAME(lineEndByte), SAME(valueStartByte)});
    REQUIRE_DEFAULTED(PropertyDrawer, {SAME(startByte), SAME(endLineStartByte), SAME(endByte)});
    REQUIRE_DEFAULTED(ClockEntry, {SAME(lineStartByte), SAME(lineEndByte)});
    REQUIRE_DEFAULTED(LogbookDrawer, {SAME(startByte), SAME(endLineStartByte), SAME(endByte)});
}

TEST_CASE("Text and UI aggregates default-initialize every field", "[DefaultInit]") {
    using ned::text::Buffer;
    REQUIRE_DEFAULTED(Buffer::SnippetRange, {SAME(id), SAME(tabstopIndex), SAME(start), SAME(end)});
    REQUIRE_DEFAULTED(Buffer::Diagnostic, {SAME(startByte), SAME(endByte), SAME(severity)});
    REQUIRE_DEFAULTED(ned::text::ConflictHunk, {SAME(startByte), SAME(endByte), SAME(oursRange.start), SAME(oursRange.end),
                                                SAME(theirsRange.start), SAME(theirsRange.end)});
    REQUIRE_DEFAULTED(ned::text::UndoTree::SerializedNode, {SAME(id)});
    REQUIRE_DEFAULTED(ned::ui::acppanel::InlineSpan, {SAME(startColumn), SAME(columnCount), SAME(bold), SAME(code)});
    REQUIRE_DEFAULTED(ned::ui::bufferview::GutterModel::InlineDiagnostic, {SAME(severity), SAME(startByte), SAME(endByte)});
    REQUIRE_DEFAULTED(ned::ui::bufferview::RenderedColorUnderlay, {SAME(startByte), SAME(endByte)});
    REQUIRE_DEFAULTED(ned::ui::bufferview::WrapSegment, {SAME(startByte), SAME(endByte)});
    REQUIRE_DEFAULTED(ned::ui::Container::Child, {SAME(widget)});
}
