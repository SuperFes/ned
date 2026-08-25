#include "VimEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>

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
}

void VimEngine::ExitInsertToNormal(text::Buffer& buffer) {
    const KeyChord escape{false, false, false, SpecialKey::Escape, 0};
    currentCommandChords_.push_back(escape);
    if (isRecordingMacro_) {
        macroRecordingBuffer_.push_back(escape);
    }
    buffer.EndUndoGroup();
    mode_                       = Mode::Normal;
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
    if (IsPlainCharChord(chord)) {
        buffer.InsertAtPoint(text::EncodeCodepointUtf8(chord.Codepoint));
    }
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

    if (c == U'w') {
        range = inner ? InnerWord(buffer, buffer.Point(), false) : AroundWord(buffer, buffer.Point(), false);
    }
    else if (c == U'W') {
        range = inner ? InnerWord(buffer, buffer.Point(), true) : AroundWord(buffer, buffer.Point(), true);
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
        const MotionResult m = GotoFirstLine(buffer, hasCount_ ? countBuffer_ : 0);
        ResolveMotionAndAct(buffer, m, true);
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
    if (op != U'd' && op != U'y') {
        statusText_ = "Visual block: operator not supported";
        return;
    }
    const std::size_t lineA     = LineOf(buffer, anchor);
    const std::size_t lineB     = LineOf(buffer, point);
    const std::size_t startLine = std::min(lineA, lineB);
    const std::size_t endLine   = std::max(lineA, lineB);
    const std::size_t colA      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineA), anchor, TabWidth());
    const std::size_t colB      = buffer.VisualColumnForByteOffset(LineStart(buffer, lineB), point, TabWidth());
    const std::size_t colStart  = std::min(colA, colB);
    const std::size_t colEnd    = std::max(colA, colB);

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
        default:
            break;
    }
    FinishCommand(buffer);
}

void VimEngine::BeginInsertSession(text::Buffer& /*buffer*/) {
    mode_ = Mode::Insert;
}

void VimEngine::EnterVisual(text::Buffer& buffer, Mode kind) {
    mode_         = kind;
    visualAnchor_ = buffer.Point();
}

void VimEngine::RememberVisualRange(text::Buffer& buffer) {
    const std::size_t l1 = LineOf(buffer, visualAnchor_);
    const std::size_t l2 = LineOf(buffer, buffer.Point());
    lastVisualRange_     = ExRange{true, std::min(l1, l2), std::max(l1, l2)};
}

void VimEngine::PasteRegister(text::Buffer& buffer, bool before, long count) {
    const char32_t regName                   = pendingRegisterName_;
    pendingRegisterName_                     = 0;
    const std::optional<RegisterEntry> entry = registers_.Get(regName);
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
        const std::size_t line               = LineOf(buffer, buffer.Point());
        const bool        atVeryEndNoNewline = !LineHasTrailingNewline(buffer, line);
        std::string       block;
        for (const std::string& p : entry->pieces) {
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

void VimEngine::JoinLines(text::Buffer& buffer, long count) {
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
        const bool needSpace = thisEnd > LineStart(buffer, line) && firstNonBlankNext < nextContentEnd;
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
        const auto it = marks_.find(name);
        if (it == marks_.end()) {
            statusText_ = "E20: Mark not set";
            FinishCommand(buffer);
            return;
        }
        target = std::min(it->second, buffer.Content().ByteLength());
    }
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

void VimEngine::RunSearch(text::Buffer& buffer, bool forward, const std::string& pattern) {
    try {
        const RegexPattern        re(pattern);
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

void VimEngine::ExecuteSubstitute(text::Buffer& buffer, const ExCommand& cmd) {
    const auto args = ParseSubstituteArgs(cmd.rest);
    if (!args || args->pattern.empty()) {
        statusText_ = "E486: Pattern not found";
        return;
    }
    const bool global = args->flags.find('g') != std::string::npos;
    try {
        const RegexPattern re(args->pattern);
        const std::size_t  startLine = cmd.range.present ? cmd.range.startLine : LineOf(buffer, buffer.Point());
        const std::size_t  endLine   = cmd.range.present ? cmd.range.endLine : startLine;
        buffer.BeginUndoGroup();
        std::size_t count = 0;
        for (std::size_t line = endLine + 1; line-- > startLine;) {
            const std::size_t ls       = LineStart(buffer, line);
            const std::size_t ce       = LineContentEnd(buffer, line);
            const std::string original = buffer.Content().Substring(ls, ce - ls);
            std::string       replaced;
            if (global) {
                const auto r = re.ReplaceAll(original, args->replacement);
                replaced     = r.text;
                count += r.count;
            }
            else {
                const auto m = re.Search(original, 0);
                if (m) {
                    replaced = original.substr(0, m->start) + re.FormatReplacement(original, *m, args->replacement) +
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
    catch (const RegexPatternError& e) {
        statusText_ = e.what();
    }
}

} // namespace ned::editor::vim
