#include "Multibuffer.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::multibuffer {

void MultibufferIndex::SetSpans(std::vector<ExcerptSpan> spans) {
    std::sort(spans.begin(), spans.end(),
              [](const ExcerptSpan& a, const ExcerptSpan& b) { return a.compositeStartByte < b.compositeStartByte; });
    spans_ = std::move(spans);
}

const ExcerptSpan* MultibufferIndex::SpanAtOffset(std::size_t compositeByteOffset) const {
    for (const ExcerptSpan& span : spans_) {
        if (compositeByteOffset >= span.compositeStartByte && compositeByteOffset < span.compositeEndByte) {
            return &span;
        }
    }
    return nullptr;
}

const std::vector<ExcerptSpan>& MultibufferIndex::Spans() const {
    return spans_;
}

void MultibufferIndex::SetLineTints(std::vector<std::pair<std::size_t, LineTint>> tints) {
    std::sort(tints.begin(), tints.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    lineTints_ = std::move(tints);
}

LineTint MultibufferIndex::TintForLine(std::size_t compositeLine) const {
    const auto it = std::lower_bound(lineTints_.begin(), lineTints_.end(), compositeLine,
                                     [](const auto& entry, std::size_t line) { return entry.first < line; });
    return (it != lineTints_.end() && it->first == compositeLine) ? it->second : LineTint::None;
}

namespace {

    // Buffer* identity -> its MultibufferIndex. See this header's own doc
    // comment for why this isn't mutex-guarded the way the process-wide
    // settings modules elsewhere in Editor/ are.
    std::unordered_map<const text::Buffer*, MultibufferIndex>& Registry() {
        static std::unordered_map<const text::Buffer*, MultibufferIndex> registry;
        return registry;
    }

} // namespace

MultibufferIndex* MultibufferIndexFor(const text::Buffer& buffer) {
    auto&      registry = Registry();
    const auto it       = registry.find(&buffer);
    return it == registry.end() ? nullptr : &it->second;
}

void SetMultibufferIndexFor(text::Buffer& buffer, MultibufferIndex index) {
    Registry()[&buffer] = std::move(index);
}

void ClearMultibufferIndexFor(const text::Buffer& buffer) {
    Registry().erase(&buffer);
}

void ClearRegistryForTesting() {
    Registry().clear();
}

std::string ReadExcerptText(text::BufferList& bufferList, const std::filesystem::path& path, std::size_t startLine,
                            std::size_t endLine) {
    std::string fullText;
    if (text::Buffer* open = bufferList.FindByPath(path)) {
        fullText = open->Text();
    }
    else {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {};
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        fullText = contents.str();
    }

    // Slice [startLine, endLine] (1-indexed, inclusive) out of fullText by
    // scanning newlines -- a plain linear scan, not Rope-backed, since this
    // runs once per excerpt at build time, not per frame.
    std::size_t line       = 1;
    std::size_t pos        = 0;
    std::size_t sliceStart = std::string::npos;
    while (pos <= fullText.size()) {
        if (line == startLine) {
            sliceStart = pos;
        }
        if (line == endLine + 1 || pos == fullText.size()) {
            return sliceStart == std::string::npos ? std::string() : fullText.substr(sliceStart, pos - sliceStart);
        }
        const std::size_t next = fullText.find('\n', pos);
        pos                    = (next == std::string::npos) ? fullText.size() : next + 1;
        ++line;
    }
    return {};
}

namespace {

    // A fixed-width box-drawing rule -- baked as literal buffer content
    // (this is a static, read-only view, not something that reflows with
    // the viewport) framing every excerpt's own title line ("ASCII
    // outline" follow-up: bold titles alone were hard to pick out of a
    // long scroll of hunks). U+2500 is the same light-horizontal glyph
    // family Border.h's own box-drawing already uses elsewhere in the UI.
    constexpr int         kRuleWidth = 78;
    constexpr const char* kRuleGlyph = "─";

    std::string MakeRuleLine() {
        std::string rule;
        rule.reserve(static_cast<std::size_t>(kRuleWidth) * 3);
        for (int i = 0; i < kRuleWidth; ++i) {
            rule += kRuleGlyph;
        }
        return rule;
    }

} // namespace

text::Buffer& BuildMultibuffer(text::BufferList& bufferList, const std::string& name,
                               const std::vector<ExcerptSource>& excerpts) {
    std::string                                   composite;
    std::vector<ExcerptSpan>                      spans;
    std::vector<std::pair<std::size_t, LineTint>> lineTints;
    spans.reserve(excerpts.size());

    // 0-indexed, matching Rope::ByteOffsetToLine's own convention -- the
    // running composite line number as text is appended, so each body
    // line's LineTint can be recorded against the exact line it lands on
    // without a second pass over the finished text.
    std::size_t compositeLine = 0;

    for (const ExcerptSource& excerpt : excerpts) {
        // A rule line ahead of every excerpt (including the first) --
        // doubles as the separator from whatever came before, and gives
        // each excerpt's own title line a visible top edge. Outside every
        // span (spanStart is captured after it), so clicking it is a
        // no-op the same way the old blank separator line already was.
        composite += MakeRuleLine();
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Rule);
        ++compositeLine;

        const std::size_t spanStart = composite.size();
        if (!excerpt.headerText.empty()) {
            composite += excerpt.headerText;
            composite += '\n';
            lineTints.emplace_back(compositeLine, LineTint::Header);
            ++compositeLine;
        }

        // Body lines are appended one at a time (rather than the whole
        // string in one shot) specifically to pair each with its own
        // composite line number for lineTints -- see this loop's role in
        // the header comment above.
        std::size_t bodyLineIndex = 0;
        std::size_t bodyPos       = 0;
        while (bodyPos < excerpt.bodyText.size()) {
            const std::size_t eol     = excerpt.bodyText.find('\n', bodyPos);
            const std::size_t lineEnd = (eol == std::string::npos) ? excerpt.bodyText.size() : eol;
            composite.append(excerpt.bodyText, bodyPos, lineEnd - bodyPos);
            composite += '\n';
            if (bodyLineIndex < excerpt.lineTints.size() && excerpt.lineTints[bodyLineIndex] != LineTint::None) {
                lineTints.emplace_back(compositeLine, excerpt.lineTints[bodyLineIndex]);
            }
            ++compositeLine;
            ++bodyLineIndex;
            bodyPos = (eol == std::string::npos) ? excerpt.bodyText.size() : eol + 1;
        }

        if (!composite.empty() && composite.back() != '\n') {
            composite += '\n';
        }
        const std::size_t spanEnd = composite.size();
        spans.push_back(ExcerptSpan{excerpt.sourcePath, excerpt.sourceStartLine, excerpt.sourceEndLine, spanStart, spanEnd});

        // A blank line of breathing room between this excerpt's own body
        // and the next rule (or the closing rule after the last excerpt) --
        // outside the span, same as the rule line itself, so it's a no-op
        // to click on.
        composite += '\n';
        ++compositeLine;
    }

    if (!excerpts.empty()) {
        // A closing rule so the last excerpt gets a visible bottom edge
        // too, matching every other excerpt's own top-rule framing.
        composite += MakeRuleLine();
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Rule);
        ++compositeLine;
    }

    text::Buffer& results = bufferList.CreateBuffer(name);
    results.InsertAtPoint(composite);
    results.SetPoint(0);
    // read-only-buffers follow-up: same reasoning as BuildResultsBuffer's
    // own doc comment -- a synthesized, no-file-to-save-to buffer.
    results.SetReadOnly(true);

    MultibufferIndex index;
    index.SetSpans(std::move(spans));
    index.SetLineTints(std::move(lineTints));
    SetMultibufferIndexFor(results, std::move(index));

    return results;
}

} // namespace ned::editor::multibuffer
