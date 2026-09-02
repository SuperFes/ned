#include "NextError.h"

#include <regex>

#include "Multibuffer.h"
#include "Text/Buffer.h"
#include "Text/ITextStorage.h"

namespace ned::editor {

namespace {
std::optional<std::string> gLastResultsBuffer;
// nullopt = "haven't stepped through the current results buffer yet" --
// StepResultLocation's own doc comment explains why this can't just be
// derived from the results buffer's Point().
std::optional<std::size_t> gCursorIndex;
} // namespace

void SetLastResultsBuffer(const std::string& bufferName) {
    gLastResultsBuffer = bufferName;
    gCursorIndex.reset();
}

std::optional<std::string> LastResultsBuffer() {
    return gLastResultsBuffer;
}

std::optional<ErrorLocation> StepResultLocation(const std::vector<ErrorLocation>& locations, bool forward) {
    if (locations.empty()) {
        return std::nullopt;
    }

    if (forward) {
        if (!gCursorIndex) {
            gCursorIndex = 0;
            return locations.front();
        }
        if (*gCursorIndex + 1 >= locations.size()) {
            return std::nullopt; // already at the last entry
        }
        gCursorIndex = *gCursorIndex + 1;
        return locations[*gCursorIndex];
    }

    if (!gCursorIndex || *gCursorIndex == 0) {
        return std::nullopt; // never stepped yet, or already at the first entry
    }
    gCursorIndex = *gCursorIndex - 1;
    return locations[*gCursorIndex];
}

void ClearLastResultsBufferForTesting() {
    gLastResultsBuffer.reset();
    gCursorIndex.reset();
}

std::vector<ErrorLocation> CollectResultLocations(const text::Buffer& buffer) {
    std::vector<ErrorLocation> locations;

    if (multibuffer::MultibufferIndex* index = multibuffer::MultibufferIndexFor(buffer)) {
        // Spans() is already sorted by compositeStartByte (MultibufferIndex::
        // SetSpans sorts on the way in), so this is already walk order.
        for (const multibuffer::ExcerptSpan& span : index->Spans()) {
            if (span.sourceStartLine == 0) {
                continue; // no single source line applies -- ExcerptSpan's own documented convention
            }
            locations.push_back(ErrorLocation{span.sourcePath, span.sourceStartLine, span.compositeStartByte});
        }
        return locations;
    }

    // Same pattern BufferView::VisitResultUnderPoint's own flat-buffer
    // fallback uses -- see that method's doc comment for why the greedy
    // `.*` is correct even when the path itself contains a ':'.
    static const std::regex   resultLinePattern(R"(^(.*):(\d+):)");
    const text::ITextStorage& content = buffer.Content();
    for (std::size_t line = 0; line < content.LineCount(); ++line) {
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

        std::smatch match;
        if (!std::regex_search(lineText, match, resultLinePattern)) {
            continue;
        }
        try {
            locations.push_back(ErrorLocation{std::filesystem::path(match[1].str()), std::stoul(match[2].str()), lineStart});
        }
        catch (const std::exception&) {
            continue; // malformed line number -- skip this line rather than aborting the whole scan
        }
    }
    return locations;
}

} // namespace ned::editor
