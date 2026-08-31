#include "VimEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include "Editor/HugeRegexScan.h"
#include "Editor/Keymap.h"
#include "Editor/RegexPattern.h"
#include "Editor/TabWidth.h"
#include "Text/Grapheme.h"
#include "Text/Utf8.h"
#include "VimLineUtil.h"
#include "VimMotion.h"
#include "VimTextObject.h"

namespace ned::editor::vim {

namespace {

    // Internal-only operator codes for g~/gu/gU -- Unicode private-use values, never
    // producible by a real keystroke, so they can share pendingOperator_'s char32_t slot
    // with the plain d/c/y/>/< operators without colliding with 'u' (undo) or any other
    // live binding.
    constexpr char32_t kOpLowercase  = 0xE001;
    constexpr char32_t kOpUppercase  = 0xE002;
    constexpr char32_t kOpToggleCase = 0xE003;

    // marks_ storage key for the `` / '' jump-back position -- 0xE004 rather than a real
    // mark letter for the same reason as the operator codes above (never producible by a
    // real 'm'-prefixed keystroke, so it can't collide with a user-set mark).
    constexpr char32_t kJumpMark = 0xE004;

    bool IsPlainCharChord(const KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == SpecialKey::None && chord.Codepoint != 0;
    }

    bool IsPlainChar(const KeyChord& chord, char32_t c) {
        return IsPlainCharChord(chord) && chord.Codepoint == c;
    }

    bool IsDigitChord(const KeyChord& chord) {
        return IsPlainCharChord(chord) && chord.Codepoint >= U'0' && chord.Codepoint <= U'9';
    }

    std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        std::size_t              pos = 0;
        while (pos <= text.size()) {
            const std::size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) {
                if (pos < text.size()) {
                    lines.push_back(text.substr(pos));
                }
                break;
            }
            lines.push_back(text.substr(pos, nl - pos));
            pos = nl + 1;
        }
        if (lines.empty()) {
            lines.emplace_back();
        }
        return lines;
    }

    // C-a/C-x's own scan: the nearest run of ASCII digits at-or-after point on the
    // current line only (real vim's own scope -- never crosses a line boundary). A
    // digit run point is already inside counts as "at point," matching real vim; a
    // leading '-' immediately before the digits is treated as a negative sign, even if
    // it's really a hyphen separator (e.g. "item-5") -- a documented, intentional match
    // of real vim's own quirky behavior here, not a bug.
    struct NumberAtPoint {
        bool        found          = false;
        std::size_t start          = 0; // absolute byte offset, sign included if present
        std::size_t end            = 0; // exclusive
        long        value          = 0;
        bool        hasLeadingZero = false; // preserve zero-padded width on rewrite
        int         originalDigits = 0;
    };

    NumberAtPoint FindNumberAtOrAfterPoint(const text::Buffer& buffer, std::size_t point) {
        const std::size_t line      = LineOf(buffer, point);
        const std::size_t lineStart = LineStart(buffer, line);
        const std::size_t lineEnd   = LineContentEnd(buffer, line);
        if (point < lineStart || point > lineEnd) {
            return {};
        }
        const std::string text    = buffer.Content().Substring(lineStart, lineEnd - lineStart);
        const auto        isDigit = [](char c) { return c >= '0' && c <= '9'; };

        std::size_t scanFrom = point - lineStart;
        if (scanFrom < text.size() && isDigit(text[scanFrom])) {
            while (scanFrom > 0 && isDigit(text[scanFrom - 1])) {
                --scanFrom;
            }
        }
        else {
            while (scanFrom < text.size() && !isDigit(text[scanFrom])) {
                ++scanFrom;
            }
            if (scanFrom >= text.size()) {
                return {};
            }
        }

        std::size_t runStart = scanFrom;
        std::size_t runEnd   = scanFrom;
        while (runEnd < text.size() && isDigit(text[runEnd])) {
            ++runEnd;
        }
        const bool negative = runStart > 0 && text[runStart - 1] == '-';
        if (negative) {
            --runStart;
        }

        NumberAtPoint result;
        try {
            result.value = std::stol(text.substr(runStart + (negative ? 1 : 0), runEnd - runStart - (negative ? 1 : 0)));
        }
        catch (const std::exception&) {
            return {}; // digit run too long to fit a long -- leave untouched
        }
        result.found          = true;
        result.start          = lineStart + runStart;
        result.end            = lineStart + runEnd;
        result.originalDigits = static_cast<int>(runEnd - scanFrom);
        result.hasLeadingZero = result.originalDigits > 1 && text[scanFrom] == '0';
        if (negative) {
            result.value = -result.value;
        }
        return result;
    }

    // Insert-mode's own small set of Ctrl-chord bindings -- real vim's C-w/C-u/C-t/C-d/
    // C-r, none of which are ordinary self-insert-command typing. Deliberately NOT
    // spliced into the pane's shared KeymapStack (WindowManager.cpp's
    // {janetKeymap, mode_.keymap, globalKeymap}) -- that stack is also what plain
    // Emacs-style editing dispatches through when vim mode is off (or always, for every
    // mode besides this one), so a permanent extra layer there would steal these chords
    // from non-vim users too. Consulted directly by HandleInsertModeChord instead, only
    // while Mode::Insert is live, via the same Keymap::Resolve machinery every other
    // keymap in this codebase uses -- just not the shared stack.
    const Keymap& InsertModeKeymap() {
        static const Keymap keymap = [] {
            Keymap km;
            km.Bind(ParseKeySequence("C-w"), "delete-word-back");
            km.Bind(ParseKeySequence("C-u"), "delete-to-line-start");
            km.Bind(ParseKeySequence("C-t"), "indent-line");
            km.Bind(ParseKeySequence("C-d"), "outdent-line");
            km.Bind(ParseKeySequence("C-r"), "insert-register");
            km.Bind(ParseKeySequence("C-o"), "one-shot-normal");
            return km;
        }();
        return keymap;
    }

} // namespace

Mode VimEngine::CurrentMode() const {
    return mode_;
}

const std::string& VimEngine::StatusText() const {
    return statusText_;
}

void VimEngine::SetViewport(std::size_t topLine, std::size_t height) {
    topLine_        = topLine;
    viewportHeight_ = height;
}

PendingIntent VimEngine::TakePendingIntent() {
    const PendingIntent intent = pendingIntent_;
    pendingIntent_             = PendingIntent::None;
    return intent;
}

std::optional<std::size_t> VimEngine::TakePendingTopLine() {
    const std::optional<std::size_t> line = pendingTopLine_;
    pendingTopLine_                       = std::nullopt;
    return line;
}

std::string VimEngine::ModeIndicator() const {
    switch (mode_) {
        case Mode::Normal:
            return "NORMAL";
        case Mode::Insert:
            return "INSERT";
        case Mode::Replace:
            return "REPLACE";
        case Mode::Visual:
            return "VISUAL";
        case Mode::VisualLine:
            return "V-LINE";
        case Mode::VisualBlock:
            return "V-BLOCK";
        case Mode::CommandLine:
            return "COMMAND";
    }
    return "";
}

long VimEngine::EffectiveCount() const {
    const long motionCount = hasCount_ ? std::max<long>(1, countBuffer_) : 1;
    if (pendingOperator_ && operatorCount_ > 0) {
        return operatorCount_ * motionCount;
    }
    return motionCount;
}

void VimEngine::FinishCommand(text::Buffer& buffer) {
    if (mode_ != Mode::Normal) {
        return; // stays accumulating currentCommandChords_ across a Visual/Insert/Replace session
    }
    if (oneShotNormalPending_) {
        // Insert-mode C-o: the one Normal-mode command (possibly an operator+motion) just
        // fully resolved -- resume Insert rather than finalizing/clearing the still-open
        // Insert session's own dot-repeat recording (which keeps accumulating exactly the
        // way a Visual-mode session's does across this same early-return, above).
        oneShotNormalPending_ = false;
        mode_                 = Mode::Insert;
        pendingOperator_.reset();
        hasCount_            = false;
        countBuffer_         = 0;
        operatorCount_       = 0;
        pendingRegisterName_ = 0;
        return;
    }
    if (buffer.ContentGeneration() != generationBeforeCommand_) {
        lastChange_ = currentCommandChords_;
    }
    currentCommandChords_.clear();
    pendingOperator_.reset();
    hasCount_            = false;
    countBuffer_         = 0;
    operatorCount_       = 0;
    pendingRegisterName_ = 0;
}

void VimEngine::UpdateGoalColumn(const text::Buffer& buffer) {
    const std::size_t line = LineOf(buffer, buffer.Point());
    goalColumn_            = buffer.VisualColumnForByteOffset(LineStart(buffer, line), buffer.Point(), TabWidth());
}

// ---------------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------------

void VimEngine::HandleKey(text::Buffer& buffer, const KeyChord& chord) {
    statusText_.clear();

    if (pendingCharHandler_) {
        CharHandler handler = std::move(pendingCharHandler_);
        pendingCharHandler_ = nullptr;
        handler(buffer, chord);
        return;
    }

    if (isRecordingMacro_ && mode_ == Mode::Normal && !pendingOperator_ && IsPlainChar(chord, U'q')) {
        StopMacroRecording();
        return;
    }
    if (isRecordingMacro_) {
        macroRecordingBuffer_.push_back(chord);
    }

    switch (mode_) {
        case Mode::CommandLine:
            HandleCommandLineKey(buffer, chord);
            return;
        case Mode::Insert:
            HandleInsertKeyDirectly(buffer, chord);
            return;
        case Mode::Replace:
            HandleReplaceKey(buffer, chord);
            return;
        case Mode::Normal:
        case Mode::Visual:
        case Mode::VisualLine:
        case Mode::VisualBlock:
            HandleNormalOrVisualKey(buffer, chord);
            return;
    }
}

void VimEngine::RecordInsertKey(const KeyChord& chord) {
    currentCommandChords_.push_back(chord);
    if (isRecordingMacro_) {
        macroRecordingBuffer_.push_back(chord);
    }
    // The "." register's own memory -- a documented v1 simplification: doesn't model
    // Backspace/C-w/C-u removing already-typed characters, so it may over-capture text
    // later deleted within the same Insert session.
    if (IsPlainCharChord(chord)) {
        insertModeTypedText_ += text::EncodeCodepointUtf8(chord.Codepoint);
    }
    else if (chord.Special == SpecialKey::Enter) {
        insertModeTypedText_ += '\n';
    }
    else if (chord.Special == SpecialKey::Tab) {
        insertModeTypedText_ += '\t';
    }
}

void VimEngine::ExitInsertToNormal(text::Buffer& buffer) {
    const KeyChord escape{false, false, false, SpecialKey::Escape, 0};
    currentCommandChords_.push_back(escape);
    if (isRecordingMacro_) {
        macroRecordingBuffer_.push_back(escape);
    }
    buffer.EndUndoGroup();
    mode_                       = Mode::Normal;
    lastInsertExitPoint_        = buffer.Point(); // gi's own memory, before the point-back adjustment below
    lastInsertedText_           = insertModeTypedText_;
    const std::size_t lineStart = LineStart(buffer, LineOf(buffer, buffer.Point()));
    if (buffer.Point() > lineStart) {
        buffer.SetPoint(text::PreviousGraphemeBoundary(buffer.Content(), buffer.Point()));
    }
    UpdateGoalColumn(buffer);
    FinishCommand(buffer);
    statusText_.clear();
}

void VimEngine::HandleInsertKeyDirectly(text::Buffer& buffer, const KeyChord& chord) {
    // Only exercised during "."/macro replay -- live Insert-mode typing bypasses
    // VimEngine entirely at the BufferView level (see this file's own header comment).
    // A simplified direct-edit path: no auto-pair/snippet-trigger/ghost-completion
    // during replay, a documented v1 simplification.
    if (chord.Special == SpecialKey::Escape) {
        ExitInsertToNormal(buffer);
        return;
    }
    currentCommandChords_.push_back(chord);
    if (chord.Special == SpecialKey::Backspace) {
        buffer.DeleteBackwardAtPoint();
        return;
    }
    if (chord.Special == SpecialKey::Enter) {
        buffer.InsertAtPoint("\n");
        return;
    }
    if (chord.Special == SpecialKey::Tab) {
        buffer.InsertAtPoint("\t");
        return;
    }
    if (HandleInsertModeChord(buffer, chord)) {
        return;
    }
    if (IsPlainCharChord(chord)) {
        buffer.InsertAtPoint(text::EncodeCodepointUtf8(chord.Codepoint));
    }
}

bool VimEngine::HandleInsertModeChord(text::Buffer& buffer, const KeyChord& chord) {
    if (awaitingInsertRegisterName_) {
        awaitingInsertRegisterName_ = false;
        if (IsPlainCharChord(chord)) {
            InsertRegisterAtPoint(buffer, chord.Codepoint);
        }
        return true;
    }
    if (!chord.Control || chord.Meta || chord.Special != SpecialKey::None) {
        return false;
    }
    const Keymap::Lookup lookup = InsertModeKeymap().Resolve({chord});
    if (lookup.result != Keymap::LookupResult::Match) {
        return false;
    }
    if (lookup.commandName == "delete-word-back") {
        DeleteWordBackInInsert(buffer);
    }
    else if (lookup.commandName == "delete-to-line-start") {
        DeleteToLineStartInInsert(buffer);
    }
    else if (lookup.commandName == "indent-line") {
        ShiftInsertLine(buffer, true);
    }
    else if (lookup.commandName == "outdent-line") {
        ShiftInsertLine(buffer, false);
    }
    else if (lookup.commandName == "insert-register") {
        awaitingInsertRegisterName_ = true;
    }
    else if (lookup.commandName == "one-shot-normal") {
        BeginOneShotNormal(buffer);
    }
    return true;
}

void VimEngine::BeginOneShotNormal(text::Buffer& buffer) {
    (void)buffer;
    oneShotNormalPending_ = true;
    mode_                 = Mode::Normal;
    // Deliberately no buffer.EndUndoGroup() here -- the Insert session's own undo group
    // (opened by whichever i/a/o/... command started it) stays open, so the one-shot
    // command's edits (if any) join it as one nestable group, matching real vim's own
    // "the whole Insert session undoes as one step, C-o excursions included" behavior.
}

void VimEngine::DeleteWordBackInInsert(text::Buffer& buffer) {
    const std::size_t  point = buffer.Point();
    const MotionResult m     = WordBackward(buffer, point, 1, false);
    if (m.target < point) {
        buffer.DeleteRange(m.target, point - m.target);
        buffer.SetPoint(m.target);
    }
}

void VimEngine::DeleteToLineStartInInsert(text::Buffer& buffer) {
    const std::size_t point = buffer.Point();
    const std::size_t ls    = LineStart(buffer, LineOf(buffer, point));
    if (point > ls) {
        buffer.DeleteRange(ls, point - ls);
        buffer.SetPoint(ls);
    }
}

void VimEngine::ShiftInsertLine(text::Buffer& buffer, bool more) {
    const std::size_t point = buffer.Point();
    const std::size_t line  = LineOf(buffer, point);
    const std::size_t ls    = LineStart(buffer, line);
    const std::size_t width = static_cast<std::size_t>(std::max(1, TabWidth()));
    if (more) {
        buffer.InsertAt(ls, std::string(width, ' '));
        buffer.SetPoint(point + width);
        return;
    }
    const std::size_t contentEnd  = LineContentEnd(buffer, line);
    std::size_t       removeCount = 0;
    while (removeCount < width && ls + removeCount < contentEnd) {
        const char32_t cp = buffer.Content().CodepointAt(ls + removeCount).codepoint;
        if (cp != U' ' && cp != U'\t') {
            break;
        }
        ++removeCount;
    }
    if (removeCount == 0) {
        return;
    }
    buffer.DeleteRange(ls, removeCount);
    buffer.SetPoint(point > ls + removeCount ? point - removeCount : ls);
}

std::optional<RegisterEntry> VimEngine::ReadRegister(const text::Buffer& buffer, char32_t name) const {
    if (name == U'/') {
        if (!lastSearchPattern_) {
            return std::nullopt;
        }
        return RegisterEntry{{*lastSearchPattern_}, RegisterKind::Char};
    }
    if (name == U':') {
        if (lastExCommandText_.empty()) {
            return std::nullopt;
        }
        return RegisterEntry{{lastExCommandText_}, RegisterKind::Char};
    }
    if (name == U'.') {
        if (lastInsertedText_.empty()) {
            return std::nullopt;
        }
        return RegisterEntry{{lastInsertedText_}, RegisterKind::Char};
    }
    if (name == U'%') {
        if (!buffer.Path()) {
            return std::nullopt;
        }
        return RegisterEntry{{buffer.Path()->string()}, RegisterKind::Char};
    }
    return registers_.Get(name);
}

void VimEngine::InsertRegisterAtPoint(text::Buffer& buffer, char32_t name) {
    const std::optional<RegisterEntry> entry = ReadRegister(buffer, name);
    if (!entry) {
        return;
    }
    const std::string text  = entry->Joined();
    const std::size_t point = buffer.Point();
    buffer.InsertAt(point, text);
    buffer.SetPoint(point + text.size());
}

void VimEngine::HandleReplaceKey(text::Buffer& buffer, const KeyChord& chord) {
    currentCommandChords_.push_back(chord);
    if (chord.Special == SpecialKey::Escape) {
        buffer.EndUndoGroup();
        mode_                       = Mode::Normal;
        const std::size_t lineStart = LineStart(buffer, LineOf(buffer, buffer.Point()));
        if (buffer.Point() > lineStart) {
            buffer.SetPoint(text::PreviousGraphemeBoundary(buffer.Content(), buffer.Point()));
        }
        UpdateGoalColumn(buffer);
        FinishCommand(buffer);
        statusText_.clear();
        return;
    }
    if (chord.Special == SpecialKey::Backspace) {
        buffer.DeleteBackwardAtPoint(); // v1 simplification: doesn't restore an overwritten character
        return;
    }
    if (IsPlainCharChord(chord) || chord.Special == SpecialKey::Enter) {
        const std::size_t p           = buffer.Point();
        const std::size_t contentEnd  = LineContentEnd(buffer, LineOf(buffer, p));
        const std::string replacement = chord.Special == SpecialKey::Enter ? "\n" : text::EncodeCodepointUtf8(chord.Codepoint);
        if (p < contentEnd) {
            const std::size_t next = text::NextGraphemeBoundary(buffer.Content(), p);
            buffer.DeleteRange(p, next - p);
        }
        buffer.InsertAt(p, replacement);
        buffer.SetPoint(p + replacement.size());
    }
}

void VimEngine::HandleCommandLineKey(text::Buffer& buffer, const KeyChord& chord) {
    if (chord.Special == SpecialKey::Escape) {
        mode_ = Mode::Normal;
        commandLineText_.clear();
        commandLinePrefix_ = 0;
        statusText_.clear();
        return;
    }
    if (chord.Special == SpecialKey::Enter) {
        const char32_t    prefix = commandLinePrefix_;
        const std::string text   = commandLineText_;
        mode_                    = Mode::Normal;
        commandLineText_.clear();
        commandLinePrefix_ = 0;
        if (prefix == U':') {
            lastExCommandText_ = text; // the ":" register -- the raw command-line text as
                                       // typed, not re-set by :g's own recursive
                                       // per-line sub-command calls into ExecuteExCommand
            ExecuteExCommand(buffer, text);
        }
        else {
            PerformSearch(buffer, prefix == U'/', text);
        }
        return;
    }
    if (chord.Special == SpecialKey::Backspace) {
        if (!commandLineText_.empty()) {
            text::RemoveLastCodepoint(commandLineText_);
            statusText_ = std::string(1, static_cast<char>(commandLinePrefix_)) + commandLineText_;
        }
        else {
            mode_              = Mode::Normal;
            commandLinePrefix_ = 0;
            statusText_.clear();
        }
        return;
    }
    if (IsPlainCharChord(chord)) {
        commandLineText_ += text::EncodeCodepointUtf8(chord.Codepoint);
    }
    statusText_ = std::string(1, static_cast<char>(commandLinePrefix_)) + commandLineText_;
}

// ---------------------------------------------------------------------------------
// Normal / Visual grammar
// ---------------------------------------------------------------------------------

void VimEngine::HandleNormalOrVisualKey(text::Buffer& buffer, const KeyChord& chord) {
    if (mode_ == Mode::Normal && !pendingOperator_ && !pendingCharHandler_ && IsPlainChar(chord, U'.')) {
        RepeatLastChange(buffer);
        return;
    }

    if (currentCommandChords_.empty()) {
        generationBeforeCommand_ = buffer.ContentGeneration();
    }
    currentCommandChords_.push_back(chord);

    if (chord.Special == SpecialKey::Escape) {
        if (mode_ != Mode::Normal) {
            RememberVisualRange(buffer);
            mode_ = Mode::Normal;
        }
        FinishCommand(buffer);
        return;
    }

    if (IsDigitChord(chord) && !(chord.Codepoint == U'0' && !hasCount_)) {
        countBuffer_ = countBuffer_ * 10 + static_cast<long>(chord.Codepoint - U'0');
        hasCount_    = true;
        return;
    }

    if (!pendingOperator_ && IsPlainChar(chord, U'"')) {
        pendingCharHandler_ = [this](text::Buffer&, const KeyChord& c) {
            if (IsPlainCharChord(c)) {
                pendingRegisterName_ = c.Codepoint;
            }
        };
        return;
    }

    if (mode_ == Mode::Normal && IsPlainChar(chord, U'g')) {
        pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) { HandleGPrefixed(buf, c); };
        return;
    }

    if (mode_ == Mode::Normal && IsPlainChar(chord, U'z')) {
        pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) { HandleZPrefixed(buf, c); };
        return;
    }

    if (mode_ == Mode::Normal && IsPlainChar(chord, U'Z')) {
        pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) { HandleCapitalZPrefixed(buf, c); };
        return;
    }

    if (mode_ != Mode::Normal && HandleVisualSpecific(buffer, chord, EffectiveCount())) {
        return;
    }

    // g~/gu/gU double as "g~~"/"guu"/"gUU" -- the second operator letter repeats bare,
    // with no second "g" prefix, matching real vim's own shorthand.
    if (mode_ == Mode::Normal && pendingOperator_) {
        char32_t doubledBy = 0;
        if (*pendingOperator_ == kOpLowercase) {
            doubledBy = U'u';
        }
        else if (*pendingOperator_ == kOpUppercase) {
            doubledBy = U'U';
        }
        else if (*pendingOperator_ == kOpToggleCase) {
            doubledBy = U'~';
        }
        if (doubledBy != 0 && IsPlainChar(chord, doubledBy)) {
            ApplyDoubledOperator(buffer, *pendingOperator_, EffectiveCount());
            FinishCommand(buffer);
            return;
        }
    }

    if (mode_ == Mode::Normal) {
        if (const auto op = ResolveOperatorChord(chord)) {
            if (pendingOperator_ && *pendingOperator_ == *op) {
                ApplyDoubledOperator(buffer, *op, EffectiveCount());
                FinishCommand(buffer);
                return;
            }
            operatorCount_   = hasCount_ ? countBuffer_ : 0;
            hasCount_        = false;
            countBuffer_     = 0;
            pendingOperator_ = op;
            return;
        }
    }

    if (IsPlainCharChord(chord) && (chord.Codepoint == U'f' || chord.Codepoint == U'F' || chord.Codepoint == U't' || chord.Codepoint == U'T')) {
        const bool forward  = chord.Codepoint == U'f' || chord.Codepoint == U't';
        const bool till     = chord.Codepoint == U't' || chord.Codepoint == U'T';
        const long count    = EffectiveCount();
        pendingCharHandler_ = [this, forward, till, count](text::Buffer& buf, const KeyChord& c) {
            if (!IsPlainCharChord(c)) {
                FinishCommand(buf);
                return;
            }
            lastFindChar_        = c.Codepoint;
            lastFindForward_     = forward;
            lastFindTill_        = till;
            hasLastFind_         = true;
            const MotionResult m = FindChar(buf, buf.Point(), count, c.Codepoint, forward, till);
            ResolveMotionAndAct(buf, m, false);
        };
        return;
    }

    if ((pendingOperator_ || mode_ != Mode::Normal) && IsPlainCharChord(chord) && (chord.Codepoint == U'i' || chord.Codepoint == U'a')) {
        const bool inner    = chord.Codepoint == U'i';
        pendingCharHandler_ = [this, inner](text::Buffer& buf, const KeyChord& c) { ApplyTextObject(buf, inner, c); };
        return;
    }

    const long count    = EffectiveCount();
    const bool vertical = chord.Special == SpecialKey::Down || chord.Special == SpecialKey::Up ||
                          IsPlainChar(chord, U'j') || IsPlainChar(chord, U'k');
    if (const auto motion = TryImmediateMotion(buffer, chord, count)) {
        ResolveMotionAndAct(buffer, *motion, !vertical);
        return;
    }

    HandleAction(buffer, chord, count);
}

std::optional<char32_t> VimEngine::ResolveOperatorChord(const KeyChord& chord) const {
    if (!IsPlainCharChord(chord)) {
        return std::nullopt;
    }
    switch (chord.Codepoint) {
        case U'd':
        case U'c':
        case U'y':
        case U'>':
        case U'<':
            return chord.Codepoint;
        default:
            return std::nullopt;
    }
}

std::optional<MotionResult> VimEngine::TryImmediateMotion(const text::Buffer& buffer, const KeyChord& chord, long count) {
    if (chord.Special == SpecialKey::Left) {
        return CharLeft(buffer, buffer.Point(), count);
    }
    if (chord.Special == SpecialKey::Right) {
        return CharRight(buffer, buffer.Point(), count);
    }
    if (chord.Special == SpecialKey::Down) {
        return LineDown(buffer, buffer.Point(), count, goalColumn_, TabWidth());
    }
    if (chord.Special == SpecialKey::Up) {
        return LineUp(buffer, buffer.Point(), count, goalColumn_, TabWidth());
    }
    if (!IsPlainCharChord(chord)) {
        return std::nullopt;
    }

    switch (chord.Codepoint) {
        case U'h':
            return CharLeft(buffer, buffer.Point(), count);
        case U'l':
            return CharRight(buffer, buffer.Point(), count);
        case U'j':
            return LineDown(buffer, buffer.Point(), count, goalColumn_, TabWidth());
        case U'k':
            return LineUp(buffer, buffer.Point(), count, goalColumn_, TabWidth());
        case U'0':
            return LineStartMotion(buffer, buffer.Point());
        case U'^':
            return FirstNonBlankMotion(buffer, buffer.Point());
        case U'$':
            return LineEndMotion(buffer, buffer.Point(), count);
        case U'w':
        case U'W': {
            const bool bigWord = chord.Codepoint == U'W';
            // The "cw" exception: with 'c' pending and point resting on a non-blank,
            // treat w/W as e/E so change-word doesn't also eat trailing whitespace.
            if (pendingOperator_ && *pendingOperator_ == U'c' && buffer.Point() < buffer.Content().ByteLength()) {
                const char32_t cp = buffer.Content().CodepointAt(buffer.Point()).codepoint;
                if (!IsBlankChar(cp)) {
                    return WordEndForward(buffer, buffer.Point(), count, bigWord);
                }
            }
            return WordForward(buffer, buffer.Point(), count, bigWord);
        }
        case U'b':
            return WordBackward(buffer, buffer.Point(), count, false);
        case U'B':
            return WordBackward(buffer, buffer.Point(), count, true);
        case U'e':
            return WordEndForward(buffer, buffer.Point(), count, false);
        case U'E':
            return WordEndForward(buffer, buffer.Point(), count, true);
        case U'{':
            return ParagraphBackward(buffer, buffer.Point(), count);
        case U'}':
            return ParagraphForward(buffer, buffer.Point(), count);
        case U'%':
            if (!hasCount_) {
                return MatchPair(buffer, buffer.Point());
            }
            return std::nullopt; // count% (goto file-percentage) -- deliberate v1 cut
        case U'G':
            marks_[kJumpMark] = buffer.Point();
            return GotoLastLine(buffer, hasCount_ ? countBuffer_ : 0);
        case U'H':
            return ScreenTop(buffer, topLine_, viewportHeight_);
        case U'M':
            return ScreenMiddle(buffer, topLine_, viewportHeight_);
        case U'L':
            return ScreenBottom(buffer, topLine_, viewportHeight_);
        default:
            return std::nullopt;
    }
}

void VimEngine::ResolveMotionAndAct(text::Buffer& buffer, const MotionResult& motion, bool horizontal) {
    if (!motion.found) {
        FinishCommand(buffer);
        return;
    }
    if (pendingOperator_) {
        const char32_t op = *pendingOperator_;
        pendingOperator_.reset();
        ApplyOperatorRange(buffer, op, buffer.Point(), motion.target, motion.linewise, motion.inclusive);
    }
    else if (mode_ != Mode::Normal) {
        buffer.SetPoint(motion.target);
    }
    else {
        buffer.SetPoint(motion.target);
    }
    if (horizontal) {
        UpdateGoalColumn(buffer);
    }
    FinishCommand(buffer);
}

void VimEngine::ApplyOperatorRange(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t target, bool linewise,
                                   bool inclusive) {
    std::size_t start = std::min(anchor, target);
    std::size_t end   = std::max(anchor, target);
    if (inclusive && end < buffer.Content().ByteLength()) {
        end = text::NextGraphemeBoundary(buffer.Content(), end);
    }
    if (linewise) {
        const std::size_t startLine = LineOf(buffer, start);
        const std::size_t endLine   = LineOf(buffer, end);
        start                       = LineStart(buffer, startLine);
        end                         = (endLine < EffectiveLastLine(buffer)) ? LineStart(buffer, endLine + 1) : buffer.Content().ByteLength();
    }
    ApplyOperator(buffer, op, start, end, linewise);
}

void VimEngine::ApplyOperator(text::Buffer& buffer, char32_t op, std::size_t start, std::size_t end, bool linewise) {
    if (start > end) {
        std::swap(start, end);
    }
    const char32_t regName = pendingRegisterName_;
    pendingRegisterName_   = 0;

    switch (op) {
        case U'y': {
            const std::string text = buffer.Content().Substring(start, end - start);
            RegisterEntry     entry;
            entry.kind   = linewise ? RegisterKind::Line : RegisterKind::Char;
            entry.pieces = linewise ? SplitLines(text) : std::vector<std::string>{text};
            registers_.Store(regName, entry, false);
            buffer.SetPoint(std::min(start, buffer.Content().ByteLength()));
            break;
        }
        case U'd':
        case U'c': {
            const std::string text = buffer.Content().Substring(start, end - start);
            RegisterEntry     entry;
            entry.kind   = linewise ? RegisterKind::Line : RegisterKind::Char;
            entry.pieces = linewise ? SplitLines(text) : std::vector<std::string>{text};
            registers_.Store(regName, entry, true);
            buffer.BeginUndoGroup();
            if (end > start) {
                buffer.DeleteRange(start, end - start);
            }
            buffer.SetPoint(std::min(start, buffer.Content().ByteLength()));
            if (op == U'c') {
                BeginInsertSession(buffer);
            }
            else {
                buffer.EndUndoGroup();
            }
            break;
        }
        case U'>':
        case U'<':
            ShiftLines(buffer, start, end, op == U'>');
            break;
        case kOpLowercase:
        case kOpUppercase:
        case kOpToggleCase:
            ToggleCaseRange(buffer, start, end, op);
            break;
        default:
            break;
    }
}

void VimEngine::ApplyDoubledOperator(text::Buffer& buffer, char32_t op, long count) {
    const std::size_t startLine = LineOf(buffer, buffer.Point());
    const std::size_t endLine =
        std::min(EffectiveLastLine(buffer), startLine + static_cast<std::size_t>(std::max<long>(1, count) - 1));
    const std::size_t start = LineStart(buffer, startLine);
    const std::size_t end   = (endLine < EffectiveLastLine(buffer)) ? LineStart(buffer, endLine + 1) : buffer.Content().ByteLength();
    ApplyOperator(buffer, op, start, end, true);
}

void VimEngine::ApplyTextObject(text::Buffer& buffer, bool inner, const KeyChord& objectChord) {
    if (!IsPlainCharChord(objectChord)) {
        FinishCommand(buffer);
        return;
    }
    const char32_t c = objectChord.Codepoint;
    ObjectRange    range{buffer.Point(), buffer.Point(), false, false};

    const long count = EffectiveCount(); // only word/sentence objects honor a count > 1 -- see VimTextObject.h

    if (c == U'w') {
        range = inner ? InnerWord(buffer, buffer.Point(), false, count) : AroundWord(buffer, buffer.Point(), false, count);
    }
    else if (c == U'W') {
        range = inner ? InnerWord(buffer, buffer.Point(), true, count) : AroundWord(buffer, buffer.Point(), true, count);
    }
    else if (c == U'"' || c == U'\'' || c == U'`') {
        range = inner ? InnerQuote(buffer, buffer.Point(), c) : AroundQuote(buffer, buffer.Point(), c);
    }
    else if (c == U'(' || c == U')' || c == U'b') {
        range = inner ? InnerBracket(buffer, buffer.Point(), U'(', U')') : AroundBracket(buffer, buffer.Point(), U'(', U')');
    }
    else if (c == U'[' || c == U']') {
        range = inner ? InnerBracket(buffer, buffer.Point(), U'[', U']') : AroundBracket(buffer, buffer.Point(), U'[', U']');
    }
    else if (c == U'{' || c == U'}' || c == U'B') {
        range = inner ? InnerBracket(buffer, buffer.Point(), U'{', U'}') : AroundBracket(buffer, buffer.Point(), U'{', U'}');
    }
    else if (c == U'<' || c == U'>') {
        range = inner ? InnerBracket(buffer, buffer.Point(), U'<', U'>') : AroundBracket(buffer, buffer.Point(), U'<', U'>');
    }
    else if (c == U'p') {
        range = inner ? InnerParagraph(buffer, buffer.Point()) : AroundParagraph(buffer, buffer.Point());
    }
    else if (c == U's') {
        range = inner ? InnerSentence(buffer, buffer.Point(), count) : AroundSentence(buffer, buffer.Point(), count);
    }
    else if (c == U't') {
        range = inner ? InnerTag(buffer, buffer.Point()) : AroundTag(buffer, buffer.Point());
    }
    else {
        FinishCommand(buffer);
        return;
    }

    if (!range.found) {
        FinishCommand(buffer);
        return;
    }

    if (pendingOperator_) {
        const char32_t op = *pendingOperator_;
        pendingOperator_.reset();
        ApplyOperator(buffer, op, range.start, range.end, range.linewise);
    }
    else if (mode_ != Mode::Normal) {
        visualAnchor_           = range.start;
        const std::size_t point = range.end > range.start ? text::PreviousGraphemeBoundary(buffer.Content(), range.end) : range.end;
        buffer.SetPoint(point);
    }
    FinishCommand(buffer);
}

void VimEngine::HandleGPrefixed(text::Buffer& buffer, const KeyChord& chord) {
    if (!IsPlainCharChord(chord)) {
        FinishCommand(buffer);
        return;
    }
    if (chord.Codepoint == U'g') {
        marks_[kJumpMark]    = buffer.Point();
        const MotionResult m = GotoFirstLine(buffer, hasCount_ ? countBuffer_ : 0);
        ResolveMotionAndAct(buffer, m, true);
        return;
    }
    if (chord.Codepoint == U'e' || chord.Codepoint == U'E') {
        const MotionResult m = WordEndBackward(buffer, buffer.Point(), EffectiveCount(), chord.Codepoint == U'E');
        ResolveMotionAndAct(buffer, m, true);
        return;
    }
    if (chord.Codepoint == U'J') { // gJ -- join without inserting a space
        JoinLines(buffer, EffectiveCount(), /*insertSpace=*/false);
        FinishCommand(buffer);
        return;
    }
    if (chord.Codepoint == U'i') { // gi -- resume Insert where it was last exited
        buffer.SetPoint(std::min(lastInsertExitPoint_, buffer.Content().ByteLength()));
        buffer.BeginUndoGroup();
        BeginInsertSession(buffer);
        return;
    }
    if (chord.Codepoint == U'v' && mode_ == Mode::Normal) {
        if (hasLastVisual_) {
            const std::size_t clampedAnchor = std::min(lastVisualAnchor_, buffer.Content().ByteLength());
            const std::size_t clampedPoint  = std::min(lastVisualPoint_, buffer.Content().ByteLength());
            mode_                           = lastVisualKind_;
            visualAnchor_                   = clampedAnchor;
            buffer.SetPoint(clampedPoint);
            UpdateGoalColumn(buffer);
        }
        FinishCommand(buffer);
        return;
    }
    if (chord.Codepoint == U'u' || chord.Codepoint == U'U' || chord.Codepoint == U'~') {
        const char32_t code = chord.Codepoint == U'u' ? kOpLowercase : chord.Codepoint == U'U' ? kOpUppercase
                                                                                               : kOpToggleCase;
        if (mode_ != Mode::Normal) {
            ApplyVisualOperator(buffer, code);
            return;
        }
        if (pendingOperator_ && *pendingOperator_ == code) {
            ApplyDoubledOperator(buffer, code, EffectiveCount());
            FinishCommand(buffer);
            return;
        }
        operatorCount_   = hasCount_ ? countBuffer_ : 0;
        hasCount_        = false;
        countBuffer_     = 0;
        pendingOperator_ = code;
        return;
    }
    FinishCommand(buffer);
}

void VimEngine::HandleZPrefixed(text::Buffer& buffer, const KeyChord& chord) {
    if (!IsPlainCharChord(chord)) {
        FinishCommand(buffer);
        return;
    }
    if (chord.Codepoint == U'z' || chord.Codepoint == U't' || chord.Codepoint == U'b') {
        const std::size_t pointLine = LineOf(buffer, buffer.Point());
        const std::size_t height    = viewportHeight_;
        if (chord.Codepoint == U'z') {
            const std::size_t half = height / 2;
            pendingTopLine_        = pointLine > half ? pointLine - half : 0;
        }
        else if (chord.Codepoint == U't') {
            pendingTopLine_ = pointLine;
        }
        else { // 'b'
            pendingTopLine_ = pointLine + 1 > height ? pointLine + 1 - height : 0;
        }
    }
    FinishCommand(buffer);
}

void VimEngine::HandleCapitalZPrefixed(text::Buffer& buffer, const KeyChord& chord) {
    if (IsPlainChar(chord, U'Z')) { // ZZ -- save and close, same body as :wq
        buffer.Save();
        pendingIntent_ = PendingIntent::CloseBuffer;
    }
    else if (IsPlainChar(chord, U'Q')) { // ZQ -- close, same body as :q (still confirm-prompts if modified)
        pendingIntent_ = PendingIntent::CloseBuffer;
    }
    FinishCommand(buffer);
}

bool VimEngine::HandleVisualSpecific(text::Buffer& buffer, const KeyChord& chord, long count) {
    (void)count;
    if (IsPlainChar(chord, U'o')) {
        const std::size_t p = buffer.Point();
        buffer.SetPoint(visualAnchor_);
        visualAnchor_ = p;
        FinishCommand(buffer);
        return true;
    }
    if (IsPlainChar(chord, U'v')) {
        RememberVisualRange(buffer);
        mode_ = mode_ == Mode::Visual ? Mode::Normal : Mode::Visual;
        FinishCommand(buffer);
        return true;
    }
    if (IsPlainChar(chord, U'V')) {
        RememberVisualRange(buffer);
        mode_ = mode_ == Mode::VisualLine ? Mode::Normal : Mode::VisualLine;
        FinishCommand(buffer);
        return true;
    }
    if (chord.Control && chord.Codepoint == U'v' && !chord.Meta) {
        RememberVisualRange(buffer);
        mode_ = mode_ == Mode::VisualBlock ? Mode::Normal : Mode::VisualBlock;
        FinishCommand(buffer);
        return true;
    }
    if (mode_ == Mode::VisualBlock && (IsPlainChar(chord, U'I') || IsPlainChar(chord, U'A'))) {
        ApplyVisualBlockInsert(buffer, IsPlainChar(chord, U'I'));
        FinishCommand(buffer);
        return true;
    }
    if (mode_ == Mode::VisualBlock && IsPlainChar(chord, U'c')) {
        ApplyVisualBlockChange(buffer);
        FinishCommand(buffer);
        return true;
    }
    if (IsPlainChar(chord, U':')) {
        RememberVisualRange(buffer);
        mode_              = Mode::CommandLine;
        commandLinePrefix_ = U':';
        commandLineText_   = "'<,'>";
        currentCommandChords_.clear();
        return true;
    }
    if (const auto op = ResolveOperatorChord(chord)) {
        ApplyVisualOperator(buffer, *op);
        return true;
    }
    if (IsPlainChar(chord, U'~') || IsPlainChar(chord, U'u') || IsPlainChar(chord, U'U')) {
        const char32_t code = chord.Codepoint == U'~' ? kOpToggleCase : chord.Codepoint == U'u' ? kOpLowercase
                                                                                                : kOpUppercase;
        ApplyVisualOperator(buffer, code);
        return true;
    }
    return false;
}

void VimEngine::ApplyVisualOperator(text::Buffer& buffer, char32_t op) {
    const Mode activeVisual = mode_;
    RememberVisualRange(buffer);
    const std::size_t anchor = visualAnchor_;
    const std::size_t point  = buffer.Point();
    mode_                    = Mode::Normal;

    if (activeVisual == Mode::VisualBlock) {
        ApplyVisualBlockOperator(buffer, op, anchor, point);
    }
    else {
        const bool linewise = activeVisual == Mode::VisualLine;
        ApplyOperatorRange(buffer, op, anchor, point, linewise, !linewise);
    }
    FinishCommand(buffer);
}

void VimEngine::ApplyVisualBlockOperator(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t point) {
    const bool isCaseOp = op == kOpLowercase || op == kOpUppercase || op == kOpToggleCase;
    if (op != U'd' && op != U'y' && op != U'>' && op != U'<' && !isCaseOp) {
        statusText_ = "Visual block: operator not supported";
        return;
    }
    const std::size_t lineA     = LineOf(buffer, anchor);
    const std::size_t lineB     = LineOf(buffer, point);
    const std::size_t startLine = std::min(lineA, lineB);
    const std::size_t endLine   = std::max(lineA, lineB);

    // >/< shift whole lines, same as any other visual-mode indent/outdent -- matches real
    // vim, which ignores the block's column extent for this pair of operators.
    if (op == U'>' || op == U'<') {
        const std::size_t start = LineStart(buffer, startLine);
        const std::size_t end   = (endLine < EffectiveLastLine(buffer)) ? LineStart(buffer, endLine + 1) : buffer.Content().ByteLength();
        ShiftLines(buffer, start, end, op == U'>');
        return;
    }

    const std::size_t colA     = buffer.VisualColumnForByteOffset(LineStart(buffer, lineA), anchor, TabWidth());
    const std::size_t colB     = buffer.VisualColumnForByteOffset(LineStart(buffer, lineB), point, TabWidth());
    const std::size_t colStart = std::min(colA, colB);
    const std::size_t colEnd   = std::max(colA, colB);

    if (isCaseOp) {
        buffer.BeginUndoGroup();
        for (std::size_t line = startLine; line <= endLine; ++line) {
            const std::size_t contentEnd = LineContentEnd(buffer, line);
            const std::size_t from       = std::min(buffer.ByteOffsetForLineAndColumn(line, colStart, TabWidth()), contentEnd);
            const std::size_t to         = std::min(buffer.ByteOffsetForLineAndColumn(line, colEnd + 1, TabWidth()), contentEnd);
            if (to > from) {
                ToggleCaseRange(buffer, from, to, op);
            }
        }
        buffer.SetPoint(std::min(buffer.ByteOffsetForLineAndColumn(startLine, colStart, TabWidth()), LineContentEnd(buffer, startLine)));
        buffer.EndUndoGroup();
        return;
    }

    const char32_t regName = pendingRegisterName_;
    pendingRegisterName_   = 0;

    std::vector<std::string> pieces(endLine - startLine + 1);
    buffer.BeginUndoGroup();
    for (std::size_t line = endLine + 1; line-- > startLine;) {
        const std::size_t contentEnd = LineContentEnd(buffer, line);
        const std::size_t from       = std::min(buffer.ByteOffsetForLineAndColumn(line, colStart, TabWidth()), contentEnd);
        const std::size_t to         = std::min(buffer.ByteOffsetForLineAndColumn(line, colEnd + 1, TabWidth()), contentEnd);
        pieces[line - startLine]     = to > from ? buffer.Content().Substring(from, to - from) : std::string();
        if (op == U'd' && to > from) {
            buffer.DeleteRange(from, to - from);
        }
    }
    RegisterEntry entry{pieces, RegisterKind::Block};
    registers_.Store(regName, entry, op == U'd');
    buffer.SetPoint(std::min(buffer.ByteOffsetForLineAndColumn(startLine, colStart, TabWidth()), LineContentEnd(buffer, startLine)));
    buffer.EndUndoGroup();
}

void VimEngine::ApplyVisualBlockInsert(text::Buffer& buffer, bool atStart) {
    RememberVisualRange(buffer);
    const std::size_t anchor    = visualAnchor_;
    const std::size_t point     = buffer.Point();
    const std::size_t lineA     = LineOf(buffer, anchor);
    const std::size_t lineB     = LineOf(buffer, point);
    const std::size_t startLine = std::min(lineA, lineB);
    const std::size_t endLine   = std::max(lineA, lineB);
    const std::size_t colA      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineA), anchor, TabWidth());
    const std::size_t colB      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineB), point, TabWidth());
    const std::size_t col       = atStart ? std::min(colA, colB) : std::max(colA, colB) + 1;

    mode_ = Mode::Normal;
    buffer.ClearSecondaryCursors();
    buffer.BeginUndoGroup();
    for (std::size_t line = startLine; line <= endLine; ++line) {
        const std::size_t contentEnd = LineContentEnd(buffer, line);
        const std::size_t offset     = std::min(buffer.ByteOffsetForLineAndColumn(line, col, TabWidth()), contentEnd);
        if (line == startLine) {
            buffer.SetPoint(offset);
        }
        else {
            buffer.AddCursorAt(offset);
        }
    }
    blockInsertSession_ = true;
    BeginInsertSession(buffer);
}

void VimEngine::ApplyVisualBlockChange(text::Buffer& buffer) {
    RememberVisualRange(buffer);
    const std::size_t anchor    = visualAnchor_;
    const std::size_t point     = buffer.Point();
    const std::size_t lineA     = LineOf(buffer, anchor);
    const std::size_t lineB     = LineOf(buffer, point);
    const std::size_t startLine = std::min(lineA, lineB);
    const std::size_t endLine   = std::max(lineA, lineB);
    const std::size_t colA      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineA), anchor, TabWidth());
    const std::size_t colB      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineB), point, TabWidth());
    const std::size_t colStart  = std::min(colA, colB);
    const std::size_t colEnd    = std::max(colA, colB);

    mode_ = Mode::Normal;
    buffer.ClearSecondaryCursors();
    buffer.BeginUndoGroup();
    for (std::size_t line = endLine + 1; line-- > startLine;) {
        const std::size_t contentEnd = LineContentEnd(buffer, line);
        const std::size_t from       = std::min(buffer.ByteOffsetForLineAndColumn(line, colStart, TabWidth()), contentEnd);
        const std::size_t to         = std::min(buffer.ByteOffsetForLineAndColumn(line, colEnd + 1, TabWidth()), contentEnd);
        if (to > from) {
            buffer.DeleteRange(from, to - from);
        }
        if (line == startLine) {
            buffer.SetPoint(from);
        }
        else if (from <= LineContentEnd(buffer, line)) {
            buffer.AddCursorAt(from);
        }
    }
    blockInsertSession_ = true;
    BeginInsertSession(buffer);
}

void VimEngine::HandleAction(text::Buffer& buffer, const KeyChord& chord, long count) {
    if (chord.Control && !chord.Meta && chord.Special == SpecialKey::None) {
        if (chord.Codepoint == U'r') {
            for (long i = 0; i < count && buffer.CanRedo(); ++i) {
                buffer.Redo();
            }
            FinishCommand(buffer);
            return;
        }
        if (chord.Codepoint == U'v' && mode_ == Mode::Normal) {
            EnterVisual(buffer, Mode::VisualBlock);
            FinishCommand(buffer);
            return;
        }
        // Half/full-page point motions -- ScrollToShowPoint() (BufferView.cpp, called
        // right after HandleKey returns) scrolls the viewport into view on its own, the
        // same way scroll-page-down/-up already work; no pendingTopLine_ channel needed
        // here. Deliberate v1 simplification: viewportHeight_ itself is the page size
        // (no scroll-off/overlap tuning).
        if (chord.Codepoint == U'd' || chord.Codepoint == U'u' || chord.Codepoint == U'f' || chord.Codepoint == U'b') {
            const std::size_t page = std::max<std::size_t>(1, viewportHeight_);
            const std::size_t lines =
                (chord.Codepoint == U'd' || chord.Codepoint == U'u') ? std::max<std::size_t>(1, page / 2) : page;
            const bool         down = chord.Codepoint == U'd' || chord.Codepoint == U'f';
            const MotionResult m    = down ? LineDown(buffer, buffer.Point(), static_cast<long>(lines) * count, goalColumn_, TabWidth())
                                           : LineUp(buffer, buffer.Point(), static_cast<long>(lines) * count, goalColumn_, TabWidth());
            if (m.found) {
                buffer.SetPoint(m.target);
            }
            FinishCommand(buffer);
            return;
        }
        // C-e/C-y scroll the viewport by one line (times count) without moving point --
        // set via pendingTopLine_ since ScrollToShowPoint() only nudges topLine_ far
        // enough to keep point visible, which wouldn't move it at all here. Point isn't
        // nudged back into view if it scrolls off-screen -- a documented v1 cut.
        if (chord.Codepoint == U'e' || chord.Codepoint == U'y') {
            const std::size_t delta = static_cast<std::size_t>(std::max<long>(1, count));
            pendingTopLine_         = chord.Codepoint == U'e' ? topLine_ + delta : (topLine_ > delta ? topLine_ - delta : 0);
            FinishCommand(buffer);
            return;
        }
        // Increment/decrement the number under or after point. Normal mode only (v1 cut
        // -- no Visual-mode multi-number "g C-a").
        if ((chord.Codepoint == U'a' || chord.Codepoint == U'x') && mode_ == Mode::Normal) {
            const NumberAtPoint num = FindNumberAtOrAfterPoint(buffer, buffer.Point());
            if (num.found) {
                const long  newValue = num.value + (chord.Codepoint == U'a' ? count : -count);
                std::string digits   = std::to_string(std::abs(newValue));
                if (num.hasLeadingZero && static_cast<int>(digits.size()) < num.originalDigits) {
                    digits.insert(0, static_cast<std::size_t>(num.originalDigits) - digits.size(), '0');
                }
                const std::string rendered = (newValue < 0 ? "-" : "") + digits;
                buffer.BeginUndoGroup();
                buffer.DeleteRange(num.start, num.end - num.start);
                buffer.InsertAt(num.start, rendered);
                buffer.SetPoint(num.start + rendered.size() - 1);
                buffer.EndUndoGroup();
            }
            FinishCommand(buffer);
            return;
        }
    }

    if (mode_ != Mode::Normal || !IsPlainCharChord(chord)) {
        FinishCommand(buffer);
        return;
    }

    switch (chord.Codepoint) {
        case U'u':
            for (long i = 0; i < count && buffer.CanUndo(); ++i) {
                buffer.Undo();
            }
            FinishCommand(buffer);
            return;
        case U'v':
            EnterVisual(buffer, Mode::Visual);
            FinishCommand(buffer);
            return;
        case U'V':
            EnterVisual(buffer, Mode::VisualLine);
            FinishCommand(buffer);
            return;
        case U'x': {
            const std::size_t p          = buffer.Point();
            const std::size_t contentEnd = LineContentEnd(buffer, LineOf(buffer, p));
            std::size_t       target     = p;
            for (long i = 0; i < count && target < contentEnd; ++i) {
                target = text::NextGraphemeBoundary(buffer.Content(), target);
            }
            if (target > p) {
                ApplyOperator(buffer, U'd', p, target, false);
            }
            FinishCommand(buffer);
            return;
        }
        case U'X': {
            const std::size_t p         = buffer.Point();
            const std::size_t lineStart = LineStart(buffer, LineOf(buffer, p));
            std::size_t       start     = p;
            for (long i = 0; i < count && start > lineStart; ++i) {
                start = text::PreviousGraphemeBoundary(buffer.Content(), start);
            }
            if (start < p) {
                ApplyOperator(buffer, U'd', start, p, false);
            }
            FinishCommand(buffer);
            return;
        }
        case U'~': {
            const std::size_t p          = buffer.Point();
            const std::size_t line       = LineOf(buffer, p);
            const std::size_t contentEnd = LineContentEnd(buffer, line);
            std::size_t       target     = p;
            for (long i = 0; i < count && target < contentEnd; ++i) {
                target = text::NextGraphemeBoundary(buffer.Content(), target);
            }
            if (target > p) {
                ToggleCaseRange(buffer, p, target, kOpToggleCase);
                // ToggleCaseRange itself lands point at the range's start (the operator
                // convention g~/gu/gU share) -- bare ~ instead advances past what it
                // toggled, clamped the same way CharRight clamps l: never past the last
                // character of the line.
                const std::size_t lineStartOff = LineStart(buffer, line);
                const std::size_t rightmost =
                    contentEnd > lineStartOff ? text::PreviousGraphemeBoundary(buffer.Content(), contentEnd) : lineStartOff;
                buffer.SetPoint(std::min(target, rightmost));
            }
            FinishCommand(buffer);
            return;
        }
        case U'R':
            buffer.BeginUndoGroup();
            BeginReplaceSession(buffer);
            FinishCommand(buffer);
            return;
        case U's': {
            const std::size_t p          = buffer.Point();
            const std::size_t contentEnd = LineContentEnd(buffer, LineOf(buffer, p));
            std::size_t       target     = p;
            for (long i = 0; i < count && target < contentEnd; ++i) {
                target = text::NextGraphemeBoundary(buffer.Content(), target);
            }
            ApplyOperator(buffer, U'c', p, target, false);
            FinishCommand(buffer);
            return;
        }
        case U'S':
            ApplyDoubledOperator(buffer, U'c', count);
            FinishCommand(buffer);
            return;
        case U'D': {
            const std::size_t p   = buffer.Point();
            const std::size_t end = LineContentEnd(buffer, LineOf(buffer, p));
            ApplyOperator(buffer, U'd', p, end, false);
            FinishCommand(buffer);
            return;
        }
        case U'C': {
            const std::size_t p   = buffer.Point();
            const std::size_t end = LineContentEnd(buffer, LineOf(buffer, p));
            ApplyOperator(buffer, U'c', p, end, false);
            FinishCommand(buffer);
            return;
        }
        case U'Y':
            ApplyDoubledOperator(buffer, U'y', count);
            FinishCommand(buffer);
            return;
        case U'J':
            JoinLines(buffer, count);
            FinishCommand(buffer);
            return;
        case U'r':
            pendingCharHandler_ = [this, count](text::Buffer& buf, const KeyChord& c) {
                if (!IsPlainCharChord(c) && c.Special != SpecialKey::Enter) {
                    FinishCommand(buf);
                    return;
                }
                const std::size_t p          = buf.Point();
                const std::size_t contentEnd = LineContentEnd(buf, LineOf(buf, p));
                std::size_t       target     = p;
                for (long i = 0; i < count && target < contentEnd; ++i) {
                    target = text::NextGraphemeBoundary(buf.Content(), target);
                }
                const long available = static_cast<long>(SplitLines(buf.Content().Substring(p, target - p)).size()); // rough count check
                (void)available;
                std::string rep;
                if (c.Special == SpecialKey::Enter) {
                    rep = "\n";
                }
                else {
                    for (long i = 0; i < count; ++i) {
                        rep += text::EncodeCodepointUtf8(c.Codepoint);
                    }
                }
                buf.BeginUndoGroup();
                if (target > p) {
                    buf.DeleteRange(p, target - p);
                }
                buf.InsertAt(p, rep);
                buf.SetPoint(p);
                buf.EndUndoGroup();
                FinishCommand(buf);
            };
            return;
        case U'p':
            PasteRegister(buffer, false, count);
            return;
        case U'P':
            PasteRegister(buffer, true, count);
            return;
        case U'i':
            buffer.BeginUndoGroup();
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        case U'I':
            buffer.BeginUndoGroup();
            buffer.SetPoint(FirstNonBlankOffset(buffer, LineOf(buffer, buffer.Point())));
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        case U'a': {
            buffer.BeginUndoGroup();
            const std::size_t contentEnd = LineContentEnd(buffer, LineOf(buffer, buffer.Point()));
            if (buffer.Point() < contentEnd) {
                buffer.SetPoint(text::NextGraphemeBoundary(buffer.Content(), buffer.Point()));
            }
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        }
        case U'A':
            buffer.BeginUndoGroup();
            buffer.SetPoint(LineContentEnd(buffer, LineOf(buffer, buffer.Point())));
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        case U'o': {
            buffer.BeginUndoGroup();
            const std::size_t line = LineOf(buffer, buffer.Point());
            const std::size_t end  = LineContentEnd(buffer, line);
            if (LineHasTrailingNewline(buffer, line)) {
                buffer.InsertAt(end, "\n");
                buffer.SetPoint(end + 1);
            }
            else {
                buffer.InsertAt(end, "\n");
                buffer.SetPoint(end + 1);
            }
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        }
        case U'O': {
            buffer.BeginUndoGroup();
            const std::size_t line  = LineOf(buffer, buffer.Point());
            const std::size_t start = LineStart(buffer, line);
            buffer.InsertAt(start, "\n");
            buffer.SetPoint(start);
            BeginInsertSession(buffer);
            FinishCommand(buffer);
            return;
        }
        case U':':
            mode_              = Mode::CommandLine;
            commandLinePrefix_ = U':';
            commandLineText_.clear();
            currentCommandChords_.clear();
            return;
        case U'/':
            mode_              = Mode::CommandLine;
            commandLinePrefix_ = U'/';
            commandLineText_.clear();
            currentCommandChords_.clear();
            return;
        case U'?':
            mode_              = Mode::CommandLine;
            commandLinePrefix_ = U'?';
            commandLineText_.clear();
            currentCommandChords_.clear();
            return;
        case U'n':
            RepeatSearch(buffer, true);
            return;
        case U'N':
            RepeatSearch(buffer, false);
            return;
        case U'*':
            SearchWordUnderPoint(buffer, true);
            return;
        case U'#':
            SearchWordUnderPoint(buffer, false);
            return;
        case U';':
            RepeatLastFind(buffer, true);
            return;
        case U',':
            RepeatLastFind(buffer, false);
            return;
        case U'm':
            pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) {
                if (IsPlainCharChord(c)) {
                    SetMarkAt(buf, c.Codepoint);
                }
                FinishCommand(buf);
            };
            return;
        case U'`':
            pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) {
                if (IsPlainCharChord(c)) {
                    GotoMark(buf, c.Codepoint, false);
                }
                else {
                    FinishCommand(buf);
                }
            };
            return;
        case U'\'':
            pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) {
                if (IsPlainCharChord(c)) {
                    GotoMark(buf, c.Codepoint, true);
                }
                else {
                    FinishCommand(buf);
                }
            };
            return;
        case U'q':
            pendingCharHandler_ = [this](text::Buffer& buf, const KeyChord& c) {
                if (IsPlainCharChord(c)) {
                    StartMacroRecording(c.Codepoint);
                }
                FinishCommand(buf);
            };
            return;
        case U'@':
            pendingCharHandler_ = [this, count](text::Buffer& buf, const KeyChord& c) {
                if (IsPlainCharChord(c)) {
                    PlayMacro(buf, c.Codepoint, count);
                }
                else {
                    FinishCommand(buf);
                }
            };
            return;
        case U'&': { // repeat the last :s on the current line only, ignoring its own range
            if (lastSubstitute_) {
                const std::size_t line = LineOf(buffer, buffer.Point());
                try {
                    SubstituteLineRange(buffer, *lastSubstitute_, line, line);
                }
                catch (const RegexPatternError& e) {
                    statusText_ = e.what();
                }
            }
            else {
                statusText_ = "E33: No previous substitute regular expression";
            }
            FinishCommand(buffer);
            return;
        }
        default:
            break;
    }
    FinishCommand(buffer);
}

void VimEngine::BeginInsertSession(text::Buffer& /*buffer*/) {
    mode_ = Mode::Insert;
    insertModeTypedText_.clear();
    // Defensive: a one-shot C-o command that itself starts a fresh Insert session (an
    // unusual thing to type, e.g. "C-o A") reaches Mode::Insert by this path rather than
    // FinishCommand's own resume branch -- clear here too so the flag never gets stuck
    // true and hijacks some later, unrelated command. See oneShotNormalPending_'s own
    // doc comment in VimEngine.h.
    oneShotNormalPending_ = false;
}

void VimEngine::BeginReplaceSession(text::Buffer& /*buffer*/) {
    mode_                 = Mode::Replace;
    oneShotNormalPending_ = false; // see BeginInsertSession's own comment on this
}

void VimEngine::EnterVisual(text::Buffer& buffer, Mode kind) {
    mode_         = kind;
    visualAnchor_ = buffer.Point();
    oneShotNormalPending_ = false; // see BeginInsertSession's own comment on this
}

void VimEngine::RememberVisualRange(text::Buffer& buffer) {
    const std::size_t l1 = LineOf(buffer, visualAnchor_);
    const std::size_t l2 = LineOf(buffer, buffer.Point());
    lastVisualRange_     = ExRange{true, std::min(l1, l2), std::max(l1, l2)};

    // Every call site here runs while mode_ still holds the active Visual/VisualLine/
    // VisualBlock kind (each caller reassigns mode_ to Normal only after this returns).
    lastVisualAnchor_ = visualAnchor_;
    lastVisualPoint_  = buffer.Point();
    lastVisualKind_   = mode_;
    hasLastVisual_    = true;
}

void VimEngine::InsertLineBlock(text::Buffer& buffer, const std::vector<std::string>& pieces, std::size_t line, bool before) {
    const bool  atVeryEndNoNewline = !LineHasTrailingNewline(buffer, line);
    std::string block;
    for (const std::string& p : pieces) {
        block += p;
        block += '\n';
    }
    std::size_t insertAt;
    if (before) {
        insertAt = LineStart(buffer, line);
    }
    else if (atVeryEndNoNewline) {
        insertAt = buffer.Content().ByteLength();
        block    = "\n" + block.substr(0, block.size() - 1);
    }
    else {
        insertAt = LineStart(buffer, line + 1);
    }
    buffer.InsertAt(insertAt, block);
    const std::size_t pastedLine = LineOf(buffer, before ? insertAt : (atVeryEndNoNewline ? insertAt + 1 : insertAt));
    buffer.SetPoint(FirstNonBlankOffset(buffer, pastedLine));
}

void VimEngine::PasteRegister(text::Buffer& buffer, bool before, long count) {
    const char32_t regName                   = pendingRegisterName_;
    pendingRegisterName_                     = 0;
    const std::optional<RegisterEntry> entry = ReadRegister(buffer, regName);
    if (!entry) {
        statusText_ = "E353: Nothing in register";
        FinishCommand(buffer);
        return;
    }

    buffer.BeginUndoGroup();
    if (entry->kind == RegisterKind::Char) {
        std::string text;
        for (long i = 0; i < std::max<long>(1, count); ++i) {
            text += entry->Joined();
        }
        std::size_t insertAt = buffer.Point();
        if (!before && insertAt < buffer.Content().ByteLength()) {
            insertAt = text::NextGraphemeBoundary(buffer.Content(), insertAt);
        }
        buffer.InsertAt(insertAt, text);
        const std::size_t after = insertAt + text.size();
        buffer.SetPoint(after > insertAt ? text::PreviousGraphemeBoundary(buffer.Content(), after) : insertAt);
    }
    else if (entry->kind == RegisterKind::Line) {
        InsertLineBlock(buffer, entry->pieces, LineOf(buffer, buffer.Point()), before);
    }
    else { // Block
        const std::size_t startLine = LineOf(buffer, buffer.Point());
        const std::size_t col =
            buffer.VisualColumnForByteOffset(LineStart(buffer, startLine), buffer.Point(), TabWidth()) + (before ? 0 : 1);
        for (std::size_t i = 0; i < entry->pieces.size(); ++i) {
            const std::size_t line = startLine + i;
            if (line > EffectiveLastLine(buffer)) {
                buffer.InsertAt(buffer.Content().ByteLength(), "\n");
            }
            const std::size_t contentEnd = LineContentEnd(buffer, line);
            const std::size_t currentCol = buffer.VisualColumnForByteOffset(LineStart(buffer, line), contentEnd, TabWidth());
            std::size_t       insertAt;
            if (currentCol < col) {
                buffer.InsertAt(contentEnd, std::string(col - currentCol, ' '));
                insertAt = contentEnd + (col - currentCol);
            }
            else {
                insertAt = buffer.ByteOffsetForLineAndColumn(line, col, TabWidth());
            }
            buffer.InsertAt(insertAt, entry->pieces[i]);
        }
        buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(startLine, col, TabWidth()));
    }
    buffer.EndUndoGroup();
    FinishCommand(buffer);
}

void VimEngine::ShiftLines(text::Buffer& buffer, std::size_t start, std::size_t end, bool more) {
    const std::size_t startLine = LineOf(buffer, start);
    const std::size_t endLine   = end > start ? LineOf(buffer, end - 1) : startLine;
    const std::size_t width     = static_cast<std::size_t>(std::max(1, TabWidth()));

    buffer.BeginUndoGroup();
    for (std::size_t line = endLine + 1; line-- > startLine;) {
        const std::size_t ls         = LineStart(buffer, line);
        const std::size_t contentEnd = LineContentEnd(buffer, line);
        if (more) {
            if (contentEnd > ls) {
                buffer.InsertAt(ls, std::string(width, ' '));
            }
        }
        else {
            std::size_t removeCount = 0;
            while (removeCount < width && ls + removeCount < contentEnd) {
                const char32_t cp = buffer.Content().CodepointAt(ls + removeCount).codepoint;
                if (cp != U' ' && cp != U'\t') {
                    break;
                }
                ++removeCount;
            }
            if (removeCount > 0) {
                buffer.DeleteRange(ls, removeCount);
            }
        }
    }
    buffer.SetPoint(LineStart(buffer, startLine));
    buffer.EndUndoGroup();
}

void VimEngine::ToggleCaseRange(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t op) {
    std::string text = buffer.Content().Substring(start, end - start);
    for (char& ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (op == kOpLowercase) {
            ch = static_cast<char>(std::tolower(c));
        }
        else if (op == kOpUppercase) {
            ch = static_cast<char>(std::toupper(c));
        }
        else {
            if (std::isupper(c)) {
                ch = static_cast<char>(std::tolower(c));
            }
            else if (std::islower(c)) {
                ch = static_cast<char>(std::toupper(c));
            }
        }
    }
    buffer.BeginUndoGroup();
    buffer.DeleteRange(start, end - start);
    buffer.InsertAt(start, text);
    buffer.SetPoint(start);
    buffer.EndUndoGroup();
}

void VimEngine::JoinLines(text::Buffer& buffer, long count, bool insertSpace) {
    const std::size_t n = static_cast<std::size_t>(std::max<long>(1, count > 1 ? count - 1 : 1));
    buffer.BeginUndoGroup();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t line = LineOf(buffer, buffer.Point());
        if (line >= EffectiveLastLine(buffer)) {
            break;
        }
        const std::size_t thisEnd           = LineContentEnd(buffer, line);
        const std::size_t nextContentEnd    = LineContentEnd(buffer, line + 1);
        const std::size_t firstNonBlankNext = FirstNonBlankOffset(buffer, line + 1);
        const std::size_t deleteLen         = firstNonBlankNext - thisEnd;
        if (deleteLen > 0) {
            buffer.DeleteRange(thisEnd, deleteLen);
        }
        const bool needSpace = insertSpace && thisEnd > LineStart(buffer, line) && firstNonBlankNext < nextContentEnd;
        if (needSpace) {
            buffer.InsertAt(thisEnd, " ");
        }
        buffer.SetPoint(thisEnd);
    }
    buffer.EndUndoGroup();
}

void VimEngine::RepeatLastFind(text::Buffer& buffer, bool sameDirection) {
    if (!hasLastFind_) {
        FinishCommand(buffer);
        return;
    }
    const bool         forward = sameDirection ? lastFindForward_ : !lastFindForward_;
    const long         count   = EffectiveCount();
    const MotionResult m       = FindChar(buffer, buffer.Point(), count, lastFindChar_, forward, lastFindTill_);
    ResolveMotionAndAct(buffer, m, false);
}

void VimEngine::RepeatLastChange(text::Buffer& buffer) {
    hasCount_    = false;
    countBuffer_ = 0;
    if (lastChange_.empty()) {
        return;
    }
    const std::vector<KeyChord> chords = lastChange_;
    currentCommandChords_.clear();
    if (replayDepth_ > 50) {
        return;
    }
    ++replayDepth_;
    for (const KeyChord& c : chords) {
        HandleKey(buffer, c);
    }
    --replayDepth_;
}

void VimEngine::SetMarkAt(text::Buffer& buffer, char32_t name) {
    marks_[name] = buffer.Point();
}

void VimEngine::GotoMark(text::Buffer& buffer, char32_t name, bool linewise) {
    std::size_t target;
    if (name == U'<' || name == U'>') {
        if (!lastVisualRange_) {
            FinishCommand(buffer);
            return;
        }
        target = LineStart(buffer, name == U'<' ? lastVisualRange_->startLine : lastVisualRange_->endLine);
    }
    else {
        // `` and '' (a repeated backtick/quote) jump to the position before the last
        // jump -- stored under kJumpMark rather than a real mark letter, since it's never
        // set via 'm'.
        const char32_t lookupName = (name == U'`' || name == U'\'') ? kJumpMark : name;
        const auto     it         = marks_.find(lookupName);
        if (it == marks_.end()) {
            statusText_ = "E20: Mark not set";
            FinishCommand(buffer);
            return;
        }
        target = std::min(it->second, buffer.Content().ByteLength());
    }
    // Every successful jump -- including through kJumpMark itself -- overwrites kJumpMark
    // with the position being left, so `` / '' toggles between the last two positions.
    marks_[kJumpMark] = buffer.Point();
    if (linewise) {
        target = FirstNonBlankOffset(buffer, LineOf(buffer, target));
    }
    buffer.SetPoint(target);
    UpdateGoalColumn(buffer);
    FinishCommand(buffer);
}

void VimEngine::StartMacroRecording(char32_t name) {
    const bool     isUpper = name >= U'A' && name <= U'Z';
    const char32_t lower   = isUpper ? name - U'A' + U'a' : name;
    if (!(lower >= U'a' && lower <= U'z')) {
        return;
    }
    isRecordingMacro_       = true;
    recordingMacroRegister_ = lower;
    macroRecordingBuffer_.clear();
    if (isUpper) {
        const auto it = macros_.find(lower);
        if (it != macros_.end()) {
            macroRecordingBuffer_ = it->second;
        }
    }
    statusText_ = "recording @" + std::string(1, static_cast<char>(lower));
}

void VimEngine::StopMacroRecording() {
    if (!isRecordingMacro_) {
        return;
    }
    macros_[recordingMacroRegister_] = macroRecordingBuffer_;
    isRecordingMacro_                = false;
    statusText_.clear();
}

void VimEngine::PlayMacro(text::Buffer& buffer, char32_t name, long count) {
    const char32_t reg = name == U'@' ? lastMacroRegister_ : name;
    if (!(reg >= U'a' && reg <= U'z')) {
        FinishCommand(buffer);
        return;
    }
    const auto it = macros_.find(reg);
    if (it == macros_.end()) {
        statusText_ = "E354: Invalid register name";
        FinishCommand(buffer);
        return;
    }
    lastMacroRegister_                 = reg;
    const std::vector<KeyChord> chords = it->second;
    currentCommandChords_.clear();
    if (replayDepth_ > 50) {
        FinishCommand(buffer);
        return;
    }
    ++replayDepth_;
    for (long i = 0; i < std::max<long>(1, count); ++i) {
        for (const KeyChord& c : chords) {
            HandleKey(buffer, c);
        }
    }
    --replayDepth_;
}

namespace {

    // huge-file-vim-search follow-up: RunSearch's huge-buffer branch below reads via
    // this a few bytes at a time (not text::NextCodepointBoundary(buffer.Text(), ...),
    // which would materialize the whole buffer just to step past one codepoint).
    std::size_t NextCodepointBoundaryInBuffer(const text::Buffer& buffer, std::size_t offset) {
        const std::size_t total = buffer.Content().ByteLength();
        if (offset >= total) {
            return total;
        }
        const std::string window = buffer.Content().Substring(offset, std::min<std::size_t>(4, total - offset));
        return offset + text::NextCodepointBoundary(window, 0);
    }

    // huge-file-vim-search follow-up: the huge-buffer branch of RunSearch, windowed via
    // Editor/HugeRegexScan.h (shared with QueryReplace's own huge-buffer path) instead
    // of materializing buffer.Text(). Same forward/backward-with-wraparound semantics as
    // the in-memory path below; returns the match's absolute start offset, or nullopt if
    // the pattern has no match anywhere in the document even after wrapping.
    std::optional<std::size_t> RunSearchHuge(const text::Buffer& buffer, const RegexPattern& re, bool forward) {
        if (forward) {
            const std::size_t total = buffer.Content().ByteLength();
            const std::size_t from =
                buffer.Point() < total ? NextCodepointBoundaryInBuffer(buffer, buffer.Point()) : total;
            std::optional<HugeRegexMatch> found = FindNextRegexMatchHuge(buffer, re, from);
            if (!found.has_value()) {
                found = FindNextRegexMatchHuge(buffer, re, 0); // wrap
            }
            if (!found.has_value()) {
                return std::nullopt;
            }
            return found->windowStart + found->match.start;
        }

        std::optional<HugeRegexMatch> found = FindLastRegexMatchHugeBefore(buffer, re, buffer.Point());
        if (!found.has_value()) {
            // wrap: take the very last match anywhere.
            found = FindLastRegexMatchHugeBefore(buffer, re, buffer.Content().ByteLength());
        }
        if (!found.has_value()) {
            return std::nullopt;
        }
        return found->windowStart + found->match.start;
    }

} // namespace

void VimEngine::RunSearch(text::Buffer& buffer, bool forward, const std::string& pattern) {
    marks_[kJumpMark] = buffer.Point(); // /, ?, n, N, *, # all funnel through here
    try {
        const RegexPattern re(pattern);

        if (buffer.Content().IsHuge()) {
            const std::optional<std::size_t> found = RunSearchHuge(buffer, re, forward);
            if (found.has_value()) {
                buffer.SetPoint(*found);
                UpdateGoalColumn(buffer);
            }
            else {
                statusText_ = "E486: Pattern not found: " + pattern;
            }
            FinishCommand(buffer);
            return;
        }

        const std::string         text = buffer.Text();
        std::optional<RegexMatch> found;
        if (forward) {
            const std::size_t from = buffer.Point() < text.size() ? text::NextCodepointBoundary(text, buffer.Point()) : text.size();
            found                  = re.Search(text, from);
            if (!found) {
                found = re.Search(text, 0); // wrap
            }
        }
        else {
            std::size_t from = 0;
            while (from <= text.size()) {
                const auto m = re.Search(text, from);
                if (!m) {
                    break;
                }
                if (m->start < buffer.Point()) {
                    found = m;
                    from  = m->start + 1;
                }
                else {
                    break;
                }
            }
            if (!found) { // wrap: take the very last match anywhere
                std::size_t from2 = 0;
                while (from2 <= text.size()) {
                    const auto m = re.Search(text, from2);
                    if (!m) {
                        break;
                    }
                    found = m;
                    from2 = m->start + 1;
                }
            }
        }
        if (found) {
            buffer.SetPoint(found->start);
            UpdateGoalColumn(buffer);
        }
        else {
            statusText_ = "E486: Pattern not found: " + pattern;
        }
    }
    catch (const RegexPatternError& e) {
        statusText_ = e.what();
    }
    FinishCommand(buffer);
}

void VimEngine::PerformSearch(text::Buffer& buffer, bool forward, const std::string& pattern) {
    if (pattern.empty()) {
        if (!lastSearchPattern_) {
            statusText_ = "E35: No previous regular expression";
            FinishCommand(buffer);
            return;
        }
    }
    else {
        lastSearchPattern_ = pattern;
    }
    lastSearchForward_ = forward;
    RunSearch(buffer, forward, *lastSearchPattern_);
}

void VimEngine::RepeatSearch(text::Buffer& buffer, bool sameDirection) {
    if (!lastSearchPattern_) {
        statusText_ = "E35: No previous regular expression";
        FinishCommand(buffer);
        return;
    }
    RunSearch(buffer, sameDirection ? lastSearchForward_ : !lastSearchForward_, *lastSearchPattern_);
}

void VimEngine::SearchWordUnderPoint(text::Buffer& buffer, bool forward) {
    const ObjectRange w = InnerWord(buffer, buffer.Point(), false);
    if (!w.found || w.end <= w.start) {
        FinishCommand(buffer);
        return;
    }
    const std::string word = buffer.Content().Substring(w.start, w.end - w.start);
    std::string       escaped;
    for (const char c : word) {
        if (std::strchr(".^$|()[]{}*+?\\", c) != nullptr) {
            escaped += '\\';
        }
        escaped += c;
    }
    lastSearchPattern_ = "\\b" + escaped + "\\b";
    lastSearchForward_ = forward;
    RunSearch(buffer, forward, *lastSearchPattern_);
}

void VimEngine::ExecuteExCommand(text::Buffer& buffer, const std::string& text) {
    const std::size_t currentLine = LineOf(buffer, buffer.Point());
    const std::size_t lastLine    = EffectiveLastLine(buffer);
    const auto        cmd         = ParseExCommand(text, currentLine, lastLine, lastVisualRange_);
    if (!cmd) {
        statusText_ = "E492: Not an editor command: " + text;
        FinishCommand(buffer);
        return;
    }

    if (cmd->name.empty()) {
        if (cmd->range.present) {
            buffer.SetPoint(FirstNonBlankOffset(buffer, cmd->range.endLine));
            UpdateGoalColumn(buffer);
        }
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "s") {
        ExecuteSubstitute(buffer, *cmd);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "w" || cmd->name == "write") {
        buffer.Save();
        statusText_ = "written";
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "q" || cmd->name == "quit") {
        pendingIntent_ = PendingIntent::CloseBuffer;
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "wq" || cmd->name == "x" || cmd->name == "xit") {
        buffer.Save();
        pendingIntent_ = PendingIntent::CloseBuffer;
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "qa" || cmd->name == "qall" || cmd->name == "quitall") {
        pendingIntent_ = PendingIntent::Quit;
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "d" || cmd->name == "delete") {
        const std::size_t sl    = cmd->range.present ? cmd->range.startLine : currentLine;
        const std::size_t el    = cmd->range.present ? cmd->range.endLine : currentLine;
        const std::size_t start = LineStart(buffer, sl);
        const std::size_t end   = el < EffectiveLastLine(buffer) ? LineStart(buffer, el + 1) : buffer.Content().ByteLength();
        ApplyOperator(buffer, U'd', start, end, true);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "j" || cmd->name == "join") {
        const std::size_t sl = cmd->range.present ? cmd->range.startLine : currentLine;
        const std::size_t el = cmd->range.present ? cmd->range.endLine : currentLine + 1;
        buffer.SetPoint(LineStart(buffer, sl));
        JoinLines(buffer, static_cast<long>(el >= sl ? el - sl + 1 : 1));
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "y" || cmd->name == "yank") {
        std::string_view regArg = cmd->rest;
        while (!regArg.empty() && regArg.front() == ' ') {
            regArg.remove_prefix(1);
        }
        if (!regArg.empty()) {
            pendingRegisterName_ = static_cast<char32_t>(static_cast<unsigned char>(regArg.front()));
        }
        const std::size_t sl    = cmd->range.present ? cmd->range.startLine : currentLine;
        const std::size_t el    = cmd->range.present ? cmd->range.endLine : currentLine;
        const std::size_t start = LineStart(buffer, sl);
        const std::size_t end   = el < EffectiveLastLine(buffer) ? LineStart(buffer, el + 1) : buffer.Content().ByteLength();
        ApplyOperator(buffer, U'y', start, end, true);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "pu" || cmd->name == "put") {
        std::string_view regArg = cmd->rest;
        while (!regArg.empty() && regArg.front() == ' ') {
            regArg.remove_prefix(1);
        }
        if (!regArg.empty()) {
            pendingRegisterName_ = static_cast<char32_t>(static_cast<unsigned char>(regArg.front()));
        }
        const std::size_t targetLine = cmd->range.present ? cmd->range.endLine : currentLine;
        buffer.SetPoint(LineStart(buffer, targetLine));
        PasteRegister(buffer, /*before=*/cmd->bang, 1);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == ">" || cmd->name == "<") {
        const std::size_t sl    = cmd->range.present ? cmd->range.startLine : currentLine;
        const std::size_t el    = cmd->range.present ? cmd->range.endLine : currentLine;
        const std::size_t start = LineStart(buffer, sl);
        const std::size_t end   = el < EffectiveLastLine(buffer) ? LineStart(buffer, el + 1) : buffer.Content().ByteLength();
        ShiftLines(buffer, start, end, cmd->name == ">");
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "m" || cmd->name == "move") {
        ExecuteMoveOrCopy(buffer, *cmd, /*isMove=*/true);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "t" || cmd->name == "co" || cmd->name == "copy") {
        ExecuteMoveOrCopy(buffer, *cmd, /*isMove=*/false);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "sort") {
        ExecuteSort(buffer, *cmd);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "r" || cmd->name == "read") {
        ExecuteRead(buffer, *cmd);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "g" || cmd->name == "global") {
        ExecuteGlobal(buffer, *cmd);
        FinishCommand(buffer);
        return;
    }
    if (cmd->name == "normal" || cmd->name == "norm") {
        std::string keys = cmd->rest;
        if (!keys.empty() && keys.front() == ' ') {
            keys.erase(0, 1);
        }
        currentCommandChords_.clear();
        for (const char c : keys) {
            KeyChord kc;
            kc.Codepoint = static_cast<unsigned char>(c);
            HandleKey(buffer, kc);
        }
        FinishCommand(buffer);
        return;
    }
    statusText_ = "E492: Not an editor command: " + cmd->name;
    FinishCommand(buffer);
}

void VimEngine::SubstituteLineRange(text::Buffer& buffer, const ExSubstituteArgs& args, std::size_t startLine, std::size_t endLine) {
    const bool         global = args.flags.find('g') != std::string::npos;
    const RegexPattern re(args.pattern); // may throw RegexPatternError -- callers catch
    buffer.BeginUndoGroup();
    std::size_t count = 0;
    for (std::size_t line = endLine + 1; line-- > startLine;) {
        const std::size_t ls       = LineStart(buffer, line);
        const std::size_t ce       = LineContentEnd(buffer, line);
        const std::string original = buffer.Content().Substring(ls, ce - ls);
        std::string       replaced;
        if (global) {
            const auto r = re.ReplaceAll(original, args.replacement);
            replaced     = r.text;
            count += r.count;
        }
        else {
            const auto m = re.Search(original, 0);
            if (m) {
                replaced = original.substr(0, m->start) + re.FormatReplacement(original, *m, args.replacement) +
                           original.substr(m->end);
                ++count;
            }
            else {
                replaced = original;
            }
        }
        if (replaced != original) {
            buffer.DeleteRange(ls, ce - ls);
            buffer.InsertAt(ls, replaced);
        }
    }
    buffer.EndUndoGroup();
    buffer.SetPoint(LineStart(buffer, startLine));
    statusText_ = count > 0 ? (std::to_string(count) + " substitution" + (count == 1 ? "" : "s")) : "E486: Pattern not found";
}

void VimEngine::ExecuteSubstitute(text::Buffer& buffer, const ExCommand& cmd) {
    const auto args = ParseSubstituteArgs(cmd.rest);
    if (!args || args->pattern.empty()) {
        statusText_ = "E486: Pattern not found";
        return;
    }
    const std::size_t startLine = cmd.range.present ? cmd.range.startLine : LineOf(buffer, buffer.Point());
    const std::size_t endLine   = cmd.range.present ? cmd.range.endLine : startLine;
    try {
        SubstituteLineRange(buffer, *args, startLine, endLine);
        lastSubstitute_ = *args; // only remember a pattern that actually compiled
    }
    catch (const RegexPatternError& e) {
        statusText_ = e.what();
    }
}

void VimEngine::ExecuteGlobal(text::Buffer& buffer, const ExCommand& cmd) {
    const auto args = ParseGlobalArgs(cmd.rest);
    if (!args || args->pattern.empty()) {
        statusText_ = "E471: Argument required";
        return;
    }

    const std::size_t startLine = cmd.range.present ? cmd.range.startLine : 0;
    const std::size_t endLine   = cmd.range.present ? cmd.range.endLine : EffectiveLastLine(buffer);

    try {
        const RegexPattern       re(args->pattern);
        std::vector<std::size_t> matchingLines;
        for (std::size_t line = startLine; line <= endLine && line <= EffectiveLastLine(buffer); ++line) {
            const std::size_t ls  = LineStart(buffer, line);
            const std::size_t ce  = LineContentEnd(buffer, line);
            const bool        hit = re.Search(buffer.Content().Substring(ls, ce - ls), 0).has_value();
            if (hit != cmd.bang) { // cmd.bang (:g!/:v) inverts the match
                matchingLines.push_back(line);
            }
        }
        if (matchingLines.empty()) {
            statusText_ = "E486: Pattern not found: " + args->pattern;
            return;
        }

        // Process bottom-to-top so a sub-command that deletes/inserts lines (":g/pat/d",
        // ":g/pat/normal ...") never invalidates a not-yet-processed line number -- edits
        // at or below a given line can't shift the numbering of lines strictly above it.
        buffer.BeginUndoGroup();
        for (auto it = matchingLines.rbegin(); it != matchingLines.rend(); ++it) {
            if (*it > EffectiveLastLine(buffer)) {
                continue; // an earlier iteration's edit shrank the buffer past this line
            }
            buffer.SetPoint(LineStart(buffer, *it));
            if (!args->command.empty()) {
                ExecuteExCommand(buffer, args->command);
            }
        }
        buffer.EndUndoGroup();
    }
    catch (const RegexPatternError& e) {
        statusText_ = e.what();
    }
}

void VimEngine::ExecuteMoveOrCopy(text::Buffer& buffer, const ExCommand& cmd, bool isMove) {
    const std::size_t currentLine = LineOf(buffer, buffer.Point());
    const std::size_t lastLine    = EffectiveLastLine(buffer);
    const std::size_t sl          = cmd.range.present ? cmd.range.startLine : currentLine;
    const std::size_t el          = cmd.range.present ? cmd.range.endLine : currentLine;

    const auto destAddr = ParseExAddress(cmd.rest, currentLine, lastLine, lastVisualRange_);
    if (!destAddr) {
        statusText_ = "E14: Invalid address";
        return;
    }
    std::size_t destLine = *destAddr; // an existing line -- real vim's own semantics insert *after* it

    std::vector<std::string> lines;
    lines.reserve(el - sl + 1);
    for (std::size_t line = sl; line <= el; ++line) {
        const std::size_t ls = LineStart(buffer, line);
        const std::size_t ce = LineContentEnd(buffer, line);
        lines.push_back(buffer.Content().Substring(ls, ce - ls));
    }

    buffer.BeginUndoGroup();
    if (isMove) {
        if (destLine >= sl && destLine <= el) {
            // Destination inside the source range -- real vim errors on this too ("E134:
            // Cannot move a range of lines into itself"); silently no-op rather than
            // inventing new user-facing error text for a rare mistyped command.
            buffer.EndUndoGroup();
            return;
        }
        const std::size_t delStart = LineStart(buffer, sl);
        const std::size_t delEnd   = el < lastLine ? LineStart(buffer, el + 1) : buffer.Content().ByteLength();
        buffer.DeleteRange(delStart, delEnd - delStart);
        if (destLine > el) {
            destLine -= (el - sl + 1);
        }
    }
    InsertLineBlock(buffer, lines, destLine, /*before=*/false);
    buffer.EndUndoGroup();
}

void VimEngine::ExecuteSort(text::Buffer& buffer, const ExCommand& cmd) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    const std::size_t sl       = cmd.range.present ? cmd.range.startLine : 0;
    const std::size_t el       = cmd.range.present ? cmd.range.endLine : lastLine;

    std::vector<std::string> lines;
    lines.reserve(el - sl + 1);
    for (std::size_t line = sl; line <= el; ++line) {
        const std::size_t ls = LineStart(buffer, line);
        const std::size_t ce = LineContentEnd(buffer, line);
        lines.push_back(buffer.Content().Substring(ls, ce - ls));
    }
    std::sort(lines.begin(), lines.end());
    if (cmd.bang) {
        std::reverse(lines.begin(), lines.end());
    }

    // Preserve "buffer doesn't end in a trailing newline" if that was true before --
    // matching InsertLineBlock's own convention for the same edge case.
    const bool  noTrailingNewline = el == lastLine && !LineHasTrailingNewline(buffer, el);
    std::string block;
    for (const std::string& l : lines) {
        block += l;
        block += '\n';
    }
    if (noTrailingNewline && !block.empty()) {
        block.pop_back();
    }

    const std::size_t start = LineStart(buffer, sl);
    const std::size_t end   = el < lastLine ? LineStart(buffer, el + 1) : buffer.Content().ByteLength();
    buffer.BeginUndoGroup();
    buffer.DeleteRange(start, end - start);
    buffer.InsertAt(start, block);
    buffer.SetPoint(start);
    buffer.EndUndoGroup();
}

void VimEngine::ExecuteRead(text::Buffer& buffer, const ExCommand& cmd) {
    std::string filename = cmd.rest;
    while (!filename.empty() && filename.front() == ' ') {
        filename.erase(0, 1);
    }
    if (filename.empty()) {
        statusText_ = "E32: No file name";
        return;
    }
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        statusText_ = "E484: Can't open file " + filename;
        return;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t currentLine = LineOf(buffer, buffer.Point());
    const std::size_t targetLine  = cmd.range.present ? cmd.range.endLine : currentLine;

    buffer.BeginUndoGroup();
    InsertLineBlock(buffer, SplitLines(content), targetLine, /*before=*/false);
    buffer.EndUndoGroup();
}

} // namespace ned::editor::vim
