#include "TreeEdit.h"

#include "Editor/Parse/ChangedRanges.h"

namespace ned::editor::grammar {

namespace {

    // Row and byte column of `offset` within `text`.
    parse::abi::Point PointForByteOffset(std::string_view text, std::size_t offset) {
        std::uint32_t row       = 0;
        std::size_t   lineStart = 0;
        for (std::size_t i = 0; i < offset; ++i) {
            if (text[i] == '\n') {
                ++row;
                lineStart = i + 1;
            }
        }
        return parse::abi::Point{.row = row, .column = static_cast<std::uint32_t>(offset - lineStart)};
    }

} // namespace

parse::InputEdit InputEditFor(std::string_view oldText, std::string_view newText, const text::ChangedSpan& span) {
    return parse::InputEdit{
        .startByte   = static_cast<std::uint32_t>(span.oldStart),
        .oldEndByte  = static_cast<std::uint32_t>(span.oldEnd),
        .newEndByte  = static_cast<std::uint32_t>(span.newEnd),
        .startPoint  = PointForByteOffset(oldText, span.oldStart),
        .oldEndPoint = PointForByteOffset(oldText, span.oldEnd),
        .newEndPoint = PointForByteOffset(newText, span.newEnd),
    };
}

TreeEdit DescribeTreeEdit(const Tree& oldEdited, const Tree& newTree, const text::ChangedSpan& span) {
    TreeEdit edit{.span = span, .structuralChanges = {}};
    for (const parse::ChangedRange& range : parse::ChangedRanges(oldEdited.Green(), newTree.Green()))
        edit.structuralChanges.emplace_back(range.start.bytes, range.end.bytes);
    return edit;
}

} // namespace ned::editor::grammar
