#include "AcpPanel.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "Border.h"
#include "Editor/Acp/AcpPanelConfig.h"
#include "Editor/FuzzyMatch.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectTree.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Mirrors BufferView.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, it's a one-line predicate private to each consumer there
    // too.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    // ACP round-1-live-validation follow-up: a bare line count, not a real
    // diff -- see FormatTranscript's Kind::ToolCall case for why a full
    // diff view (unified-diff-style +/- lines) is deliberately not attempted
    // here yet. Empty text counts as zero lines, not one.
    int CountLines(const std::string& text) {
        if (text.empty()) {
            return 0;
        }
        int count = 1;
        for (const char ch : text) {
            if (ch == '\n') {
                ++count;
            }
        }
        return count;
    }

    constexpr int      kMinWidthForCloseButton    = 8;
    constexpr int      kMinWidthForMinimizeButton = 12; // needs room for both 3-glyph button groups plus a gap column
    constexpr int      kCloseOffset               = 4;  // column of '[' counted back from width, matches TerminalPanel's own offset
    constexpr int      kMinimizeOffset            = 8;  // column of '[' for the minimize button, one 3-glyph group + gap left of close
    constexpr char32_t kCloseIcon    = U'×'; // safe: one whole encoded glyph placed in exactly one Cell, not byte-indexed
    // Matches TerminalPanel's own minimize glyph exactly (its kMinimizeIcon)
    // -- glyph-consistency follow-up: this panel used a plain ASCII "-"
    // originally, reported live as inconsistent with the terminal drawer's
    // own title-row buttons.
    constexpr char32_t kMinimizeIcon = U'▼';
    constexpr int       kMaxInputRows          = 6;    // cap how far the composer grows before it starts scrolling internally
    // ACP checkpoint/rewind follow-up: the picker only ever offers a digit
    // 1-9 (PendingPermissionPrompt's own selection shape) -- older turns
    // beyond this many simply aren't reachable in one keystroke; picking one
    // of the shown 9 first, then reopening the picker again, still reaches
    // anything further back one hop at a time.
    constexpr std::size_t kMaxRewindChoices = 9;
    // @-file-mention autocomplete follow-up: an arbitrary small cap keeping
    // the suggestion list from crowding out the transcript beneath it on a
    // short panel -- ranked[0] is always the best fuzzy match regardless of
    // how many total candidates exist, so this only ever hides the weaker
    // tail of the ranking, never the top pick.
    constexpr std::size_t kMaxMentionChoices = 6;

    // Matches Editor/Backup.cpp's own LocalTimeLabel format exactly (not
    // shared -- that one's private to its .cpp) so a rewind checkpoint's
    // timestamp reads the same way a backup version's does elsewhere in
    // this editor.
    std::string LocalTimeLabel(std::chrono::system_clock::time_point timestamp) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
        std::tm           local{};
        localtime_r(&seconds, &local);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return buffer;
    }

    // Codepoint count, matching WordWrap/PaintUtf8Row's own one-column-per-
    // codepoint convention -- used for right-aligning a short marker within
    // a line's remaining width (no double-width CJK/emoji handling anywhere
    // in this codebase yet).
    int ColumnCount(std::string_view text) {
        int         columns = 0;
        std::size_t pos     = 0;
        while (pos < text.size()) {
            pos = text::NextCodepointBoundary(text, pos);
            ++columns;
        }
        return columns;
    }

    // ACP chat-feel round 2: right-aligns a short status marker within
    // `width`, keeping the left-hand text reading as continuous prose
    // instead of a status word interrupting it mid-line. Falls back to a
    // plain trailing " marker" when there isn't room -- WordWrap upstream
    // will still wrap the combined string sanely on a narrow panel, it just
    // won't look right-aligned there.
    std::string RightAlignMarker(const std::string& left, const std::string& marker, int width) {
        if (marker.empty()) {
            return left;
        }
        const int padding = width - ColumnCount(left) - ColumnCount(marker);
        if (padding < 1) {
            return left + " " + marker;
        }
        return left + std::string(static_cast<std::size_t>(padding), ' ') + marker;
    }

    // One physical row produced by wrapping a string to `width` columns.
    // startColumn/columnCount are codepoint offsets into the *original*
    // string (PaintUtf8Row's own one-column-per-codepoint convention) --
    // every codepoint lands in exactly one row and none are dropped, so a
    // flat cursor column can always be mapped back to (row, columnInRow) by
    // finding which row's [startColumn, startColumn+columnCount] it falls in.
    struct WrappedRow {
        std::string text;
        int         startColumn;
        int         columnCount;
    };

    // Greedy word-wrap: breaks before whichever codepoint would push a row
    // past `width` columns, preferring to break after the most recent space
    // in the current row, falling back to a hard mid-word break only when a
    // single word alone exceeds `width`. Always returns at least one row
    // (possibly empty), so an empty logical line still occupies one physical
    // row.
    std::vector<WrappedRow> WordWrap(std::string_view text, int width) {
        std::vector<WrappedRow> rows;
        if (width <= 0) {
            rows.push_back({std::string(text), 0, 0});
            return rows;
        }
        std::size_t rowStartByte  = 0;
        int         rowStartCol   = 0;
        int         col           = 0;
        std::size_t lastSpaceByte = std::string::npos; // byte just after the last space seen in this row
        int         lastSpaceCol  = 0;                 // col value at that point

        std::size_t pos = 0;
        while (pos < text.size()) {
            if (col >= width) {
                if (lastSpaceByte != std::string::npos && lastSpaceByte > rowStartByte) {
                    rows.push_back({std::string(text.substr(rowStartByte, lastSpaceByte - rowStartByte)), rowStartCol, lastSpaceCol});
                    rowStartCol += lastSpaceCol;
                    rowStartByte = lastSpaceByte;
                    col -= lastSpaceCol;
                }
                else {
                    rows.push_back({std::string(text.substr(rowStartByte, pos - rowStartByte)), rowStartCol, col});
                    rowStartCol += col;
                    rowStartByte = pos;
                    col = 0;
                }
                lastSpaceByte = std::string::npos;
            }
            const std::size_t next = text::NextCodepointBoundary(text, pos);
            if (text[pos] == ' ') { // safe at byte level: UTF-8 continuation/lead bytes are always >= 0x80
                lastSpaceByte = next;
                lastSpaceCol  = col + 1;
            }
            ++col;
            pos = next;
        }
        rows.push_back({std::string(text.substr(rowStartByte)), rowStartCol, col});
        return rows;
    }

    std::string StateLabel(editor::acp::AcpManager::SessionState state) {
        switch (state) {
            case editor::acp::AcpManager::SessionState::Starting:
                return "starting";
            case editor::acp::AcpManager::SessionState::Active:
                return "active";
            case editor::acp::AcpManager::SessionState::Inactive:
                return "inactive";
        }
        return "inactive";
    }

} // namespace

AcpPanel::AcpPanel(const Theme& theme) : theme_(theme), prompt_("Prompt: ") {
}

void AcpPanel::SetAcpManager(editor::acp::AcpManager* acpManager) {
    acpManager_ = acpManager;
}

void AcpPanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

void AcpPanel::OpenRewindPicker() {
    if (acpManager_) {
        rewindPickerOpen_ = true;
    }
}

bool AcpPanel::Collapsed() const {
    return collapsed_;
}

void AcpPanel::SetCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) {
        return;
    }
    collapsed_ = collapsed;
    if (onCollapseChanged_) {
        onCollapseChanged_();
    }
}

void AcpPanel::ToggleCollapsed() {
    SetCollapsed(!collapsed_);
}

void AcpPanel::SetOnCollapseChanged(std::function<void()> onCollapseChanged) {
    onCollapseChanged_ = std::move(onCollapseChanged);
}

void AcpPanel::SetTerminalSize(Size size) {
    terminalSize_ = size;
}

// ACP chat-feel round 2: shell-style prompt history, re-derived from the
// transcript's own Kind::UserMessage entries -- see this method pair's own
// doc comment in AcpPanel.h for why nothing is duplicated into a separate
// list here.
void AcpPanel::HistoryPrevious() {
    if (!acpManager_) {
        return;
    }
    std::vector<std::string> history;
    for (const auto& entry : acpManager_->Transcript()) {
        if (entry.kind == editor::acp::AcpManager::TranscriptEntry::Kind::UserMessage) {
            history.push_back(entry.text);
        }
    }
    if (history.empty()) {
        return;
    }
    if (!historyIndex_) {
        historyDraft_ = prompt_.Text();
        historyIndex_ = history.size() - 1;
    }
    else if (*historyIndex_ > 0) {
        --*historyIndex_;
    }
    prompt_.SetText(history[*historyIndex_]);
}

void AcpPanel::HistoryNext() {
    if (!historyIndex_) {
        return;
    }
    std::vector<std::string> history;
    if (acpManager_) {
        for (const auto& entry : acpManager_->Transcript()) {
            if (entry.kind == editor::acp::AcpManager::TranscriptEntry::Kind::UserMessage) {
                history.push_back(entry.text);
            }
        }
    }
    if (*historyIndex_ + 1 < history.size()) {
        ++*historyIndex_;
        prompt_.SetText(history[*historyIndex_]);
    }
    else {
        historyIndex_.reset();
        prompt_.SetText(historyDraft_);
    }
}

Brush AcpPanel::BrushForStyle(DisplayStyle style) const {
    switch (style) {
        case DisplayStyle::Dim:
            return Brush{.background = theme_.background, .foreground = theme_.commentForeground};
        case DisplayStyle::Warning:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticWarning};
        case DisplayStyle::Accent:
            return Brush{.background = theme_.background, .foreground = theme_.borderAccent.foreground};
        case DisplayStyle::Hint:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticHint};
        case DisplayStyle::Plain:
            break;
    }
    return Brush{.background = theme_.background, .foreground = theme_.defaultForeground};
}

std::vector<AcpPanel::DisplayLine> AcpPanel::FormatTranscript(int width) const {
    std::vector<DisplayLine> lines;
    if (!acpManager_) {
        return lines;
    }

    const auto& pending    = acpManager_->PendingPermissionPrompt();
    const auto& transcript = acpManager_->Transcript();
    using Kind              = editor::acp::AcpManager::TranscriptEntry::Kind;

    // ACP chat-feel round 2: which ToolCall entry is the most recent one --
    // that one alone stays fully expanded (title + status + diff-line-count
    // summary) once resolved; every earlier, already-resolved tool call
    // collapses to one compact line below. Reclaims vertical space for
    // actually-current content given this panel's own "no scrollback in v1"
    // constraint (header comment) -- a real agent turn can easily fire off a
    // dozen tool calls, and every one of them permanently holding 1-2 lines
    // was pushing the answer itself off the visible tail.
    std::size_t lastToolCallIndex = transcript.size();
    for (std::size_t i = 0; i < transcript.size(); ++i) {
        if (transcript[i].kind == Kind::ToolCall) {
            lastToolCallIndex = i;
        }
    }

    for (std::size_t i = 0; i < transcript.size(); ++i) {
        const auto& entry = transcript[i];
        switch (entry.kind) {
            case Kind::UserMessage: {
                lines.push_back({"> " + entry.text, DisplayStyle::Plain});
                break;
            }
            case Kind::AgentText:
            case Kind::AgentThought: {
                // ACP chat-feel round 2: AgentThought renders Dim, the same
                // "background chatter" style ToolCall/SessionEvent already
                // use, so a reply's own answer (Accent) visually separates
                // from the agent's private reasoning instead of both reading
                // as one undifferentiated stream -- see TranscriptEntry::
                // Kind's own doc comment for why this is a distinct Kind now,
                // not a bool.
                const DisplayStyle style = entry.kind == Kind::AgentThought ? DisplayStyle::Dim : DisplayStyle::Accent;
                // Word-wrap happens once, generically, over every logical
                // line in Paint()'s own loop below -- only literal newlines
                // the agent itself sent are split here. ACP Markdown
                // rendering follow-up: each split line's own **bold**/
                // `code`/bullet markup is stripped here too, before it ever
                // reaches WordWrap.
                std::size_t start = 0;
                while (start <= entry.text.size()) {
                    const std::size_t newlinePos = entry.text.find('\n', start);
                    const std::string rawLine =
                        newlinePos == std::string::npos ? entry.text.substr(start) : entry.text.substr(start, newlinePos - start);
                    const InlineMarkdownResult formatted = ApplyInlineMarkdown(rawLine);
                    lines.push_back({formatted.text, style, formatted.spans});
                    if (newlinePos == std::string::npos) {
                        break;
                    }
                    start = newlinePos + 1;
                }
                break;
            }
            case Kind::ToolCall: {
                // Plain ASCII marker, not a Unicode glyph -- the content-row
                // paint loop below places one *byte* per Cell (DrawBorderTitle's
                // own long-standing assumption), so a multi-byte glyph here
                // would corrupt column alignment for the rest of the line.
                const bool terminal = entry.status == "completed" || entry.status == "failed" || entry.status == "cancelled";
                const std::string marker = entry.status == "completed"   ? "[done]"
                                            : entry.status == "failed"    ? "[fail]"
                                            : entry.status == "cancelled" ? "[cancel]"
                                            : entry.status.empty()        ? std::string()
                                                                           : "[" + entry.status + "]";
                lines.push_back({RightAlignMarker("* " + entry.text, marker, width), DisplayStyle::Dim});
                // Collapse: resolved, and superseded by a later tool call --
                // see lastToolCallIndex's own doc comment above. Skips the
                // diff-summary sub-line below, keeping a resolved-and-
                // superseded call to exactly one line.
                if (terminal && i != lastToolCallIndex) {
                    break;
                }
                // A real diff view (actual +/- lines) is deliberately not
                // attempted here -- this codebase has no reusable line-diff
                // utility yet (ThreeWayMerge.h's LCS diff is a private
                // implementation detail, not an exposed API), and building
                // one from scratch is a bigger, separate piece of work. A
                // line-count delta is still a concrete improvement over a
                // bare status word -- confirms *something* changed and
                // roughly how much, without a bare "(completed)".
                if (entry.diffOldText && entry.diffNewText) {
                    const int oldLines = CountLines(*entry.diffOldText);
                    const int newLines = CountLines(*entry.diffNewText);
                    lines.push_back({"  (" + std::to_string(oldLines) + " -> " + std::to_string(newLines) + " lines)", DisplayStyle::Dim});
                }
                break;
            }
            case Kind::Plan: {
                for (const std::string& step : entry.planSteps) {
                    const InlineMarkdownResult formatted = ApplyInlineMarkdown(step);
                    lines.push_back({formatted.text, DisplayStyle::Hint, formatted.spans});
                }
                break;
            }
            case Kind::Permission: {
                lines.push_back({"! " + entry.text, DisplayStyle::Warning});
                if (pending && pending->description == entry.text) {
                    std::string options;
                    for (std::size_t optIndex = 0; optIndex < pending->options.size(); ++optIndex) {
                        if (optIndex > 0) {
                            options += "  ";
                        }
                        options += "[" + std::to_string(optIndex + 1) + "] " + pending->options[optIndex].name;
                    }
                    if (!options.empty()) {
                        lines.push_back({"  " + options, DisplayStyle::Warning});
                    }
                }
                break;
            }
            case Kind::SessionEvent: {
                lines.push_back({"-- " + entry.text + " --", DisplayStyle::Dim});
                break;
            }
        }
    }
    return lines;
}

AcpPanel::InlineMarkdownResult AcpPanel::ApplyInlineMarkdown(std::string_view raw) {
    InlineMarkdownResult result;
    std::string&         out = result.text;
    out.reserve(raw.size());

    // Leading bullet marker: "- "/"* "/"+ " after optional indentation,
    // Markdown's own unordered-list convention -- indentation is preserved
    // verbatim (nested lists stay nested), only the marker byte itself
    // becomes a bullet glyph. Never matches Kind::Plan's own "[x] "/"[~] "/
    // "[ ] " checkbox prefix (PushOrReplacePlan, AcpManager.cpp), so this
    // doesn't collide with that convention.
    std::size_t bodyStart = 0;
    {
        std::size_t indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') {
            ++indent;
        }
        if (indent + 1 < raw.size() && (raw[indent] == '-' || raw[indent] == '*' || raw[indent] == '+') && raw[indent + 1] == ' ') {
            out.append(raw.substr(0, indent));
            out += text::EncodeCodepointUtf8(U'•');
            out += ' ';
            bodyStart = indent + 2;
        }
    }

    int         col = ColumnCount(out);
    std::size_t pos = bodyStart;
    while (pos < raw.size()) {
        if (pos + 1 < raw.size() && raw[pos] == '*' && raw[pos + 1] == '*') {
            const std::size_t close = raw.find("**", pos + 2);
            if (close != std::string_view::npos && close > pos + 2) {
                const std::string_view inner    = raw.substr(pos + 2, close - (pos + 2));
                const int              startCol = col;
                out.append(inner);
                col += ColumnCount(inner);
                result.spans.push_back({.startColumn = startCol, .columnCount = col - startCol, .bold = true, .code = false});
                pos = close + 2;
                continue;
            }
        }
        if (raw[pos] == '`') {
            const std::size_t close = raw.find('`', pos + 1);
            if (close != std::string_view::npos && close > pos + 1) {
                const std::string_view inner    = raw.substr(pos + 1, close - (pos + 1));
                const int              startCol = col;
                out.append(inner);
                col += ColumnCount(inner);
                result.spans.push_back({.startColumn = startCol, .columnCount = col - startCol, .bold = false, .code = true});
                pos = close + 1;
                continue;
            }
        }
        const std::size_t next = text::NextCodepointBoundary(raw, pos);
        out.append(raw.substr(pos, next - pos));
        ++col;
        pos = next;
    }
    return result;
}

std::vector<AcpPanel::InlineSpan> AcpPanel::SpansForRow(const std::vector<InlineSpan>& spans, int rowStartColumn, int rowColumnCount) {
    std::vector<InlineSpan> result;
    const int               rowEnd = rowStartColumn + rowColumnCount;
    for (const InlineSpan& span : spans) {
        const int start = std::max(span.startColumn, rowStartColumn);
        const int end   = std::min(span.startColumn + span.columnCount, rowEnd);
        if (start < end) {
            result.push_back({.startColumn = start - rowStartColumn, .columnCount = end - start, .bold = span.bold, .code = span.code});
        }
    }
    return result;
}

void AcpPanel::PaintStyledRow(Canvas& canvas, int x, int y, std::string_view text, const std::vector<InlineSpan>& spans,
                               const Brush& baseBrush, int maxColumns) const {
    if (spans.empty() || maxColumns <= 0) {
        PaintUtf8Row(canvas, x, y, text, baseBrush, maxColumns);
        return;
    }
    // Column -> byte offset table (ColumnCount's own one-codepoint-per-column
    // convention), built once per row so each span's substr is a direct
    // lookup rather than a fresh scan from the row's own start.
    std::vector<std::size_t> offsets;
    offsets.reserve(text.size() + 1);
    std::size_t pos = 0;
    while (pos < text.size()) {
        offsets.push_back(pos);
        pos = text::NextCodepointBoundary(text, pos);
    }
    offsets.push_back(text.size());
    const int totalColumns = static_cast<int>(offsets.size()) - 1;

    int painted = 0; // columns painted so far, i.e. the x offset relative to `x`
    int col     = 0;
    for (const InlineSpan& span : spans) {
        if (painted >= maxColumns) {
            break;
        }
        const int plainEnd = std::min(span.startColumn, totalColumns);
        if (col < plainEnd) {
            const std::string_view segment = text.substr(offsets[col], offsets[plainEnd] - offsets[col]);
            painted += PaintUtf8Row(canvas, x + painted, y, segment, baseBrush, maxColumns - painted);
            col = plainEnd;
        }
        const int spanEnd = std::min(span.startColumn + span.columnCount, totalColumns);
        if (col < spanEnd && painted < maxColumns) {
            Brush spanBrush = baseBrush;
            if (span.code) {
                spanBrush.background = theme_.documentHighlightBackground;
            }
            if (span.bold) {
                spanBrush.bold = true;
            }
            const std::string_view segment = text.substr(offsets[col], offsets[spanEnd] - offsets[col]);
            painted += PaintUtf8Row(canvas, x + painted, y, segment, spanBrush, maxColumns - painted);
            col = spanEnd;
        }
    }
    if (col < totalColumns && painted < maxColumns) {
        const std::string_view segment = text.substr(offsets[col], offsets[totalColumns] - offsets[col]);
        PaintUtf8Row(canvas, x + painted, y, segment, baseBrush, maxColumns - painted);
    }
}

std::vector<AcpPanel::DisplayLine> AcpPanel::FormatRewindPicker(int /*width*/) const {
    std::vector<DisplayLine> lines;
    lines.push_back({"Rewind to before which turn?", DisplayStyle::Warning});
    if (!acpManager_) {
        return lines;
    }
    const std::size_t count = acpManager_->CheckpointCount();
    if (count == 0) {
        lines.push_back({"  (no turns recorded yet)", DisplayStyle::Dim});
        return lines;
    }
    // Newest first -- digit 1 is always the most recent turn, matching the
    // picker's own "jump back from here" framing.
    const std::size_t shown = std::min(count, kMaxRewindChoices);
    for (std::size_t offset = 0; offset < shown; ++offset) {
        const std::size_t                          index      = count - 1 - offset;
        const editor::acp::AcpManager::Checkpoint& checkpoint = acpManager_->CheckpointAt(index);
        lines.push_back({"  [" + std::to_string(offset + 1) + "] " + checkpoint.promptPreview + "  (" +
                             LocalTimeLabel(checkpoint.timestamp) + ")",
                         DisplayStyle::Plain});
    }
    lines.push_back({"  [Esc] cancel", DisplayStyle::Dim});
    return lines;
}

// @-file-mention autocomplete follow-up -- see this method's own doc comment
// in AcpPanel.h. Purely derived from (prompt_.Text(), prompt_.CursorByteOffset()):
// the current "word" is the run of non-whitespace immediately before the
// cursor; a mention is active iff that word starts with '@'.
void AcpPanel::RefreshMentionState() {
    const std::string& text   = prompt_.Text();
    const std::size_t  cursor = prompt_.CursorByteOffset();
    std::size_t         start  = cursor;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\n' && text[start - 1] != '\t') {
        --start;
    }
    if (start >= cursor || text[start] != '@') {
        mentionPickerOpen_ = false;
        return;
    }
    const bool wasOpen = mentionPickerOpen_;
    mentionStartByte_  = start;
    mentionQuery_      = text.substr(start + 1, cursor - start - 1);
    mentionPickerOpen_ = true;
    if (!wasOpen) {
        RefreshMentionCandidates(); // fresh walk each time the picker (re)opens -- see its own doc comment
    }
    const std::size_t count = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_).size();
    mentionSelection_        = count == 0 ? 0 : std::min(mentionSelection_, count - 1);
}

void AcpPanel::RefreshMentionCandidates() {
    mentionCandidates_.clear();
    mentionSelection_          = 0;
    const std::filesystem::path root = editor::ProjectRoot();
    for (const editor::ProjectTreeEntry& entry : editor::BuildProjectTree(root)) {
        if (!entry.isDirectory) {
            mentionCandidates_.push_back(std::filesystem::relative(entry.path, root).generic_string());
        }
    }
}

void AcpPanel::AcceptMentionCandidate() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_);
    mentionPickerOpen_                    = false;
    if (ranked.empty()) {
        return;
    }
    const std::size_t  index = std::min(mentionSelection_, ranked.size() - 1);
    const std::string& text  = prompt_.Text();
    const std::size_t  cursor = prompt_.CursorByteOffset();
    // MinibufferPrompt::SetText's own documented "cursor moves to the end"
    // behavior applies here (see AcpPanel.h's own doc comment on this method).
    prompt_.SetText(text.substr(0, mentionStartByte_) + "@" + ranked[index] + " " + text.substr(cursor));
}

std::vector<AcpPanel::DisplayLine> AcpPanel::FormatMentionPicker(int /*width*/) const {
    std::vector<DisplayLine> lines;
    lines.push_back({"Mention a file (Enter/Tab to insert, Esc to cancel):", DisplayStyle::Warning});
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_);
    if (ranked.empty()) {
        lines.push_back({"  (no matching files)", DisplayStyle::Dim});
        return lines;
    }
    const std::size_t shown = std::min(ranked.size(), kMaxMentionChoices);
    for (std::size_t i = 0; i < shown; ++i) {
        const bool selected = i == mentionSelection_;
        lines.push_back({(selected ? "> " : "  ") + ranked[i], selected ? DisplayStyle::Accent : DisplayStyle::Plain});
    }
    if (ranked.size() > shown) {
        lines.push_back({"  (" + std::to_string(ranked.size() - shown) + " more...)", DisplayStyle::Dim});
    }
    return lines;
}

bool AcpPanel::CloseButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForCloseButton) {
        return false;
    }
    return local.x >= width - kCloseOffset && local.x <= width - kCloseOffset + 2;
}

bool AcpPanel::MinimizeButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForMinimizeButton) {
        return false;
    }
    return local.x >= width - kMinimizeOffset && local.x <= width - kMinimizeOffset + 2;
}

// ACP chat-feel round 2: the thin strip Collapsed() renders instead of the
// full panel -- the placement lambda (main.cpp) is expected to hand this a
// short Box (one row for a bottom dock, a couple of columns for a right
// dock); this just fills whatever it's given. The whole strip is a single
// click-to-reopen target (mirroring ProjectSidebar's own collapsed-strip
// convention), not just a small button, since there's very little to aim at
// on a genuinely thin strip.
void AcpPanel::PaintCollapsedStrip(Canvas& canvas, int width, int height) const {
    if (editor::acp::GetAcpPanelDock() == editor::acp::AcpPanelDock::Right) {
        // A right-docked strip is narrow but tall, carrying no readable text
        // -- just a single expand glyph, so the border brush (tuned for thin
        // decorative lines, not text legibility) is fine here.
        const Brush& frameBrush = Focused() ? theme_.borderAccent : theme_.border;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                Cell& cell     = canvas[{.x = x, .y = y}];
                cell.character = " ";
                frameBrush.ApplyTo(cell);
            }
        }
        // Roughly centered vertically, same spirit as ProjectSidebar's own
        // kCollapsedTriangle. Points left ("expand this way"), the opposite
        // direction from the sidebar's own right-pointing glyph, since this
        // panel sits on the right edge of the screen.
        if (width > 0) {
            Cell& hint     = canvas[{.x = 0, .y = height / 2}];
            hint.character = text::EncodeCodepointUtf8(U'◂');
            frameBrush.ApplyTo(hint);
        }
        return;
    }
    // A bottom-docked strip is wide but one row tall -- room enough to show
    // the same agent-name/state title text the full panel's own title row
    // shows, so minimizing doesn't lose that at-a-glance status. Painted
    // with theme_.echoArea, not the border brush -- reported live as nearly
    // unreadable when painted with theme_.border/borderAccent (a color
    // tuned for a thin decorative line, not a full row of text); the
    // composer's own input row hit this identical problem and was fixed the
    // same way -- see this file's own comment on that fix, in Paint()'s
    // input-row section below.
    const Brush stripBrush = theme_.echoArea;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            stripBrush.ApplyTo(cell);
        }
    }
    const std::string agentName = acpManager_ && !acpManager_->AgentName().empty() ? acpManager_->AgentName() : std::string("ACP agent");
    const std::string title =
        agentName + " [" + StateLabel(acpManager_ ? acpManager_->State() : editor::acp::AcpManager::SessionState::Inactive) + "] (minimized)";
    DrawBorderTitle(canvas, title, stripBrush);
}

void AcpPanel::BeginResize(Point globalMouse) {
    resizing_            = true;
    resizeAnchorGlobal_  = globalMouse;
    resizeStartPercent_  = editor::acp::AcpPanelSizePercent();
}

void AcpPanel::UpdateResize(Point globalMouse) {
    const bool rightDock          = editor::acp::GetAcpPanelDock() == editor::acp::AcpPanelDock::Right;
    // Dragging the resize edge away from the composer grows it in both
    // docks: leftward for a right-docked panel (its own left edge is the
    // handle), upward for a bottom-docked one (its own top border is the
    // handle) -- both expressed as "anchor minus current" so a move in the
    // growing direction yields a positive delta.
    const int deltaPixels          = rightDock ? resizeAnchorGlobal_.x - globalMouse.x : resizeAnchorGlobal_.y - globalMouse.y;
    const int terminalDimension    = rightDock ? terminalSize_.width : terminalSize_.height;
    if (terminalDimension <= 0) {
        return; // SetTerminalSize never called yet -- see its own doc comment
    }
    const int deltaPercent = deltaPixels * 100 / terminalDimension;
    editor::acp::SetAcpPanelSizePercent(resizeStartPercent_ + deltaPercent);
}

void AcpPanel::EndResize() {
    resizing_ = false;
}

void AcpPanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    if (collapsed_) {
        PaintCollapsedStrip(canvas, width, height);
        return;
    }

    // Opaque fill first -- this panel floats over BufferView the same way
    // TerminalPanel does, so every cell in its Box must be painted
    // regardless of content, or the buffer beneath would show through.
    const Brush plainBrush = BrushForStyle(DisplayStyle::Plain);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            plainBrush.ApplyTo(cell);
        }
    }

    // Title/divider row.
    const Brush&      frameBrush = Focused() ? theme_.borderAccent : theme_.border;
    const std::string horizontal = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = 0}];
        cell.character = horizontal;
        frameBrush.ApplyTo(cell);
    }
    const std::string agentName = acpManager_ && !acpManager_->AgentName().empty() ? acpManager_->AgentName() : std::string("ACP agent");
    const std::string title     = agentName + " [" + StateLabel(acpManager_ ? acpManager_->State() : editor::acp::AcpManager::SessionState::Inactive) + "]";
    DrawBorderTitle(canvas, title, frameBrush);
    if (width >= kMinWidthForCloseButton) {
        const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(kCloseIcon), "]"};
        for (int i = 0; i < 3; ++i) {
            Cell& cell     = canvas[{.x = width - kCloseOffset + i, .y = 0}];
            cell.character = glyphs[i];
            frameBrush.ApplyTo(cell);
        }
    }
    if (width >= kMinWidthForMinimizeButton) {
        // TerminalPanel's own kMinimizeIcon (▼), not a plain "-" -- see
        // kMinimizeIcon's own doc comment.
        const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(kMinimizeIcon), "]"};
        for (int i = 0; i < 3; ++i) {
            Cell& cell     = canvas[{.x = width - kMinimizeOffset + i, .y = 0}];
            cell.character = glyphs[i];
            frameBrush.ApplyTo(cell);
        }
    }

    if (height < 2) {
        return;
    }

    // The composer grows to however many wrapped rows its text needs (capped
    // at kMaxInputRows, beyond which it scrolls internally like the content
    // area does) -- smart-wrapping follow-up: previously a fixed single row,
    // so a prompt longer than the panel's width just ran off-screen with no
    // way to see or correct the hidden part.
    const std::string             statusText = prompt_.StatusText();
    const std::vector<WrappedRow> inputRows  = WordWrap(statusText, width);
    const int                     totalInputCols =
        inputRows.empty() ? 0 : inputRows.back().startColumn + inputRows.back().columnCount;
    const int caretFlat = std::min(prompt_.CursorDisplayColumn(), totalInputCols);
    int       caretRow = 0, caretColInRow = 0;
    for (std::size_t i = 0; i < inputRows.size(); ++i) {
        const WrappedRow& row = inputRows[i];
        if (caretFlat <= row.startColumn + row.columnCount) {
            caretRow      = static_cast<int>(i);
            caretColInRow = caretFlat - row.startColumn;
            break;
        }
    }

    const int allottedInputRows =
        std::max(1, std::min({static_cast<int>(inputRows.size()), kMaxInputRows, std::max(1, height - 1)}));
    int inputWindowStart = 0;
    if (static_cast<int>(inputRows.size()) > allottedInputRows) {
        // Scroll the visible window to always include the caret's row --
        // the only way the cursor can leave the allotted rows is Left/Home
        // inside a prompt long enough to be capped.
        inputWindowStart = std::clamp(caretRow - allottedInputRows + 1, 0, static_cast<int>(inputRows.size()) - allottedInputRows);
    }

    // Content rows: the tail of the formatted, word-wrapped transcript that
    // fits, top-aligned within the window (i.e. the window itself is
    // anchored to the most recent lines) -- no scrollback in v1, see header
    // comment.
    const int contentRows = std::max(0, height - 1 - allottedInputRows);
    if (contentRows > 0) {
        // ACP Markdown rendering follow-up: each logical DisplayLine's own
        // spans (in its plain-text column space) are re-based onto whichever
        // WrappedRow they land in via SpansForRow, so PaintStyledRow below
        // still lands bold/inline-code styling on the right glyphs after
        // word-wrap splits a long agent reply across several physical rows.
        struct PhysicalLine {
            std::string             text;
            DisplayStyle            style;
            std::vector<InlineSpan> spans;
        };
        std::vector<PhysicalLine> lines;
        const std::vector<DisplayLine> content = rewindPickerOpen_    ? FormatRewindPicker(width)
                                                 : mentionPickerOpen_ ? FormatMentionPicker(width)
                                                                      : FormatTranscript(width);
        for (const DisplayLine& logical : content) {
            for (const WrappedRow& row : WordWrap(logical.text, width)) {
                lines.push_back({row.text, logical.style, SpansForRow(logical.spans, row.startColumn, row.columnCount)});
            }
        }
        const int start = std::max(0, static_cast<int>(lines.size()) - contentRows);
        for (int row = 0; row < contentRows; ++row) {
            const std::size_t lineIndex = static_cast<std::size_t>(start + row);
            if (lineIndex >= lines.size()) {
                continue;
            }
            const PhysicalLine& line  = lines[lineIndex];
            const Brush          brush = BrushForStyle(line.style);
            PaintStyledRow(canvas, 0, row + 1, line.text, line.spans, brush, width);
        }
    }

    // Input rows. Painted with theme_.echoArea rather than plainBrush -- the
    // same "you're being prompted, type here" brush every other prompt in
    // the editor (find-file, M-x, goto-line, ...) already uses via EchoArea,
    // so the composer reads as a distinct input field instead of blending
    // into the transcript above it (reported live as barely
    // visible/unreadable when painted the same as the rest of the panel --
    // see this panel's own ROADMAP.md entry; the underlying cursor-position
    // editing gap that entry also describes is unaffected by this, only the
    // row's legibility).
    const Brush inputBrush = theme_.echoArea;
    for (int j = 0; j < allottedInputRows; ++j) {
        const int screenRow = height - allottedInputRows + j;
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = screenRow}];
            cell.character = " ";
            inputBrush.ApplyTo(cell);
        }
        const std::size_t rowIndex = static_cast<std::size_t>(inputWindowStart + j);
        if (rowIndex < inputRows.size()) {
            PaintUtf8Row(canvas, 0, screenRow, inputRows[rowIndex].text, inputBrush, width);
        }
    }
    // minibuffer-composer-cursor-editing follow-up: the caret sits at the
    // prompt's real cursor position now, not always at the end of the typed
    // text -- CursorDisplayColumn() (one column per codepoint, matching
    // PaintUtf8Row's own convention) is what stays in sync with
    // InsertChar/DeleteBackward/DeleteForward/Move* below, mapped through
    // the wrap above into (caretRow, caretColInRow) so it still lands on the
    // right glyph once the composer spans multiple rows. A real solid block
    // cursor, not a video-invert: character is left untouched (so whatever's
    // already there -- a real typed character, or the row fill's blank when
    // the cursor sits past the end of the text -- stays visible through it)
    // and only recolored, explicitly, to a fixed high-contrast pair rather
    // than swapping whatever foreground happened to be underneath --
    // inverting inputBrush's own default-ish foreground left the character
    // under the caret nearly unreadable against the yellow block (reported
    // live after moving the cursor back over typed text).
    if (caretRow >= inputWindowStart && caretRow < inputWindowStart + allottedInputRows && caretColInRow < width) {
        const int screenRow = height - allottedInputRows + (caretRow - inputWindowStart);
        Cell&     cell      = canvas[{.x = caretColInRow, .y = screenRow}];
        Brush{.background = inputBrush.foreground, .foreground = Color::Black}.ApplyTo(cell);
    }
}

bool AcpPanel::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        const MouseEvent rawMouse = event.mouse();

        // ProjectSidebar::OnEvent's own exact resize-drag shape: Moved/
        // Released are handled against the *global* mouse position before
        // the local-bounds hit test below, since a fast drag can carry the
        // cursor outside this panel's own Box mid-session (BufferView
        // cooperates the same way once a sidebar drag crosses out of its
        // bounds -- see Widget.h's own header comment on this).
        if (rawMouse.motion == MouseEvent::Motion::Moved && resizing_) {
            UpdateResize(rawMouse.at);
            return true;
        }
        if (rawMouse.motion == MouseEvent::Motion::Released && resizing_) {
            EndResize();
            return true;
        }

        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false;
        }

        // Collapsed: the whole strip is a single click-to-reopen target --
        // there's very little to aim at on a genuinely thin strip, so no
        // separate button hit-test the way the full panel has.
        if (collapsed_) {
            if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
                SetCollapsed(false);
                TakeFocus();
            }
            return true;
        }

        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            if (CloseButtonAt(mouse->at)) {
                if (onToggleRequest_) {
                    onToggleRequest_();
                }
                return true;
            }
            if (MinimizeButtonAt(mouse->at)) {
                SetCollapsed(true);
                return true;
            }
            // The resize divider: the title row for a bottom dock (its own
            // dedicated row, not shared with any content), the panel's own
            // left edge column for a right dock (the boundary shared with
            // BufferView beneath it -- ProjectSidebar's own right-edge
            // divider, mirrored).
            const bool rightDock = editor::acp::GetAcpPanelDock() == editor::acp::AcpPanelDock::Right;
            if (rightDock ? mouse->at.x == 0 : mouse->at.y == 0) {
                BeginResize(rawMouse.at);
                TakeFocus();
                return true;
            }
            TakeFocus();
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (!chord) {
        return false;
    }

    // ACP round-1-live-validation follow-up: while a permission prompt is
    // pending, this panel resolves it directly rather than leaving
    // resolution to BufferView's separate echo-area InputMode::
    // AcpPermissionPrompt flow -- the "deliberate v1 cut" this class's own
    // header comment used to document. WindowManager's SetAcpPanelFocusChecker
    // wiring skips routing a new request to the focused pane's echo area
    // whenever this panel has focus, so this is the only place such a
    // keystroke lands in that case. Digit keys only, matching the numbered
    // list FormatTranscript already renders -- no moving selection cursor
    // the way BufferView's own flow has, so Enter is deliberately left alone
    // (no obvious default option to pick without one).
    if (acpManager_ && acpManager_->PendingPermissionPrompt()) {
        const editor::acp::AcpManager::PermissionPrompt& pending = *acpManager_->PendingPermissionPrompt();
        if (chord->Special == editor::SpecialKey::Escape) {
            acpManager_->CancelPermissionPrompt();
            return true;
        }
        if (IsPlainCharacter(*chord) && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
            const std::size_t index = static_cast<std::size_t>(chord->Codepoint - U'1');
            if (index < pending.options.size()) {
                acpManager_->ResolvePermissionPrompt(pending.options[index].optionId);
            }
            return true; // out-of-range digit: stay put, same as BufferView's own flow
        }
        // Anything else (typing into the composer, non-digit keys) falls
        // through unhandled by this block.
    }

    // ACP checkpoint/rewind follow-up: same digit-select shape as the
    // PendingPermissionPrompt block above -- while the picker is open it
    // owns every keystroke (nothing should land in the composer behind it),
    // Escape cancels with no effect, a valid digit rewinds and closes it,
    // anything else is swallowed rather than falling through.
    if (rewindPickerOpen_) {
        if (chord->Special == editor::SpecialKey::Escape) {
            rewindPickerOpen_ = false;
            return true;
        }
        if (acpManager_ && IsPlainCharacter(*chord) && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
            const std::size_t offset = static_cast<std::size_t>(chord->Codepoint - U'1');
            const std::size_t count  = acpManager_->CheckpointCount();
            if (offset < count && offset < kMaxRewindChoices) {
                acpManager_->RewindTo(count - 1 - offset);
            }
            rewindPickerOpen_ = false;
        }
        return true;
    }

    // @-file-mention autocomplete follow-up: while a mention query is active
    // (RefreshMentionState, called after every composer edit below), Up/Down/
    // Enter/Tab/Escape are claimed for narrowing/accepting/dismissing the
    // suggestion list instead of their usual composer meaning (history
    // recall / send / panel-close). Deliberately no unconditional catch-all
    // `return true` at the bottom of this block, unlike rewindPickerOpen_
    // above -- anything else (more query characters, Backspace, cursor
    // motion) must still fall through to the ordinary composer handling
    // below, which re-derives mention state itself after every edit.
    if (mentionPickerOpen_) {
        if (chord->Special == editor::SpecialKey::Escape) {
            mentionPickerOpen_ = false;
            return true;
        }
        if (chord->Special == editor::SpecialKey::Down || chord->Special == editor::SpecialKey::Up) {
            const std::size_t count = editor::FuzzyFilterAndRank(mentionCandidates_, mentionQuery_).size();
            if (count > 0) {
                mentionSelection_ = chord->Special == editor::SpecialKey::Down ? (mentionSelection_ + 1) % count
                                                                                : (mentionSelection_ + count - 1) % count;
            }
            return true;
        }
        if (chord->Special == editor::SpecialKey::Enter || chord->Special == editor::SpecialKey::Tab) {
            AcceptMentionCandidate();
            return true;
        }
    }

    if (chord->Special == editor::SpecialKey::Escape) {
        // chat-feel follow-up: interrupt a still-streaming reply first,
        // keeping whatever partial output already arrived (Claude Code's
        // own single-Esc-to-interrupt) -- a second Escape once nothing is
        // in flight falls through to the pre-existing close-panel behavior.
        if (acpManager_ && acpManager_->PromptInFlight()) {
            acpManager_->CancelPrompt();
            return true;
        }
        if (onToggleRequest_) {
            onToggleRequest_();
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Backspace) {
        prompt_.DeleteBackward();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Delete) {
        prompt_.DeleteForward();
        RefreshMentionState();
        return true;
    }
    // minibuffer-composer-cursor-editing round 2: word-wise motion, checked
    // ahead of the plain Left/Right handlers below so Control-Left/Right
    // don't fall through to a single-codepoint move.
    if (chord->Special == editor::SpecialKey::Left && chord->Control) {
        prompt_.MoveCursorWordLeft();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right && chord->Control) {
        prompt_.MoveCursorWordRight();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Left) {
        prompt_.MoveCursorLeft();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right) {
        prompt_.MoveCursorRight();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Home) {
        prompt_.MoveCursorToStart();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::End) {
        prompt_.MoveCursorToEnd();
        RefreshMentionState();
        return true;
    }
    // Keyboard resize fallback, alongside the border-drag in the mouse
    // handling above -- Control-Up/Down, checked ahead of the plain Up/Down
    // history recall below the same way Control-Left/Right is checked ahead
    // of plain Left/Right above.
    if (chord->Special == editor::SpecialKey::Up && chord->Control) {
        editor::acp::SetAcpPanelSizePercent(editor::acp::AcpPanelSizePercent() + 5);
        return true;
    }
    if (chord->Special == editor::SpecialKey::Down && chord->Control) {
        editor::acp::SetAcpPanelSizePercent(editor::acp::AcpPanelSizePercent() - 5);
        return true;
    }
    // Keyboard minimize toggle, alongside the [-] title-bar button above --
    // M-m, unused elsewhere in this composer (a plain "m" keystroke still
    // types the letter as always; only the Meta-modified chord is claimed).
    if (chord->Meta && chord->Codepoint == U'm') {
        ToggleCollapsed();
        return true;
    }
    // Shell-style prompt history -- Up/Down are otherwise unbound in this
    // composer (a single logical line, word-wrapped but with no vertical
    // intra-composer cursor movement of its own), so there's no existing
    // affordance this takes away.
    if (chord->Special == editor::SpecialKey::Up) {
        HistoryPrevious();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Down) {
        HistoryNext();
        RefreshMentionState();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Enter) {
        if (acpManager_ && !prompt_.Text().empty()) {
            acpManager_->SendPrompt(prompt_.Text());
            prompt_.SetText("");
            historyIndex_.reset();
            historyDraft_.clear();
            mentionPickerOpen_ = false;
        }
        return true;
    }
    if (IsPlainCharacter(*chord)) {
        prompt_.InsertChar(chord->Codepoint);
        RefreshMentionState();
        return true;
    }
    return false;
}

} // namespace ned::ui
