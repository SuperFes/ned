//
// The Vim modal state machine -- one per BufferView pane (mirrors PrefixArgumentReader/
// IncrementalSearch's "pure-ish, key-by-key, caller-driven" shape, scaled up to Vim's
// much larger grammar). Never touches anything beyond a Buffer& handed in per call, so
// it stays exactly as UI-free as the rest of Editor/.
//
// Deliberate v1 cuts, documented here once rather than scattered across every method
// that would otherwise need its own caveat: no jumplist/changelist (C-o/C-i) beyond the
// single toggle `` / '' provides, no mark letters beyond a-z/A-Z and '</'> (the
// visual-selection marks) -- A-Z are buffer-local here, not real vim's cross-file global
// marks, since this engine has no notion of "other buffers", macros are recorded as raw
// keystrokes (not vim's own editable-register-text form), "." replays verbatim (a count
// typed before "." is accepted but does not override the recorded one), search/:s use
// PCRE2 syntax passed straight through (Editor/RegexPattern.h) rather than translating
// vim's own default "magic" escaping convention -- closer to vim's \v very-magic mode
// than its default. Insert-mode typing itself is not handled here at all: BufferView
// forwards those keystrokes straight through its ordinary Dispatcher path (self-insert-
// command, auto-pair, snippets, ghost completion all keep working unmodified) and only
// calls RecordInsertKey so "." can replay them later -- HandleKey's own Mode::Insert
// branch (HandleInsertKeyDirectly) is a simplified direct-edit fallback exercised only
// during "."/macro replay, not live typing.
//

#ifndef NED_EDITOR_VIM_VIMENGINE_H
#define NED_EDITOR_VIM_VIMENGINE_H

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Text/Buffer.h"
#include "VimExCommand.h"
#include "VimRegisters.h"
#include "VimTypes.h"

namespace ned::editor::vim {

// An intent beyond single-buffer scope (":q", ":wq") that only the host UI (BufferView)
// can actually carry out -- CommandContext::interactiveRequest's own "command signals
// intent, host UI acts on it" shape, deliberately kept this small rather than threading
// a BufferList&/WindowManager callback into the engine itself.
enum class PendingIntent { None,
                           Quit,
                           CloseBuffer };

class VimEngine {
  public:
    VimEngine() = default;

    [[nodiscard]] Mode CurrentMode() const;

    // BufferView calls this whenever CurrentMode() != Mode::Insert.
    void HandleKey(text::Buffer& buffer, const KeyChord& chord);

    // BufferView calls this for every non-Escape chord while CurrentMode() ==
    // Mode::Insert, before deciding how to actually apply it -- dot-repeat/macro
    // bookkeeping only, independent of whichever of the two paths below ends up
    // mutating the buffer.
    void RecordInsertKey(const KeyChord& chord);

    // BufferView calls this right after RecordInsertKey, before falling through to its
    // own ordinary Dispatcher path -- vim's own small set of Insert-mode Ctrl-chords
    // (C-w/C-u/C-t/C-d/C-r; see InsertModeKeymap in the .cpp) that aren't ordinary
    // self-insert-command typing and would otherwise fire ned's Emacs bindings for the
    // same chords instead. Returns true if the chord was handled here (BufferView must
    // NOT also dispatch it normally); false means "not one of these, dispatch as usual."
    [[nodiscard]] bool HandleInsertModeChord(text::Buffer& buffer, const KeyChord& chord);

    // Escape from Insert back to Normal: closes the undo group insert-entry opened,
    // finalizes "." dot-repeat, and moves point back one grapheme (vim's own rule --
    // Normal-mode point never rests past the last character of a line).
    void ExitInsertToNormal(text::Buffer& buffer);

    [[nodiscard]] const std::string& StatusText() const;
    [[nodiscard]] std::string        ModeIndicator() const; // "NORMAL"/"INSERT"/"VISUAL"/"V-LINE"/"V-BLOCK"/"REPLACE"/"COMMAND"

    // Live viewport facts (H/M/L) -- Buffer itself has no notion of one.
    void SetViewport(std::size_t topLine, std::size_t height);

    // Set by the most recent HandleKey call that produced one; BufferView reads and
    // consumes it right after.
    [[nodiscard]] PendingIntent TakePendingIntent();

  private:
    using CharHandler = std::function<void(text::Buffer&, const KeyChord&)>;

    // ---- top-level per-mode dispatch ----
    void HandleNormalOrVisualKey(text::Buffer& buffer, const KeyChord& chord);
    void HandleReplaceKey(text::Buffer& buffer, const KeyChord& chord);
    void HandleCommandLineKey(text::Buffer& buffer, const KeyChord& chord);
    void HandleInsertKeyDirectly(text::Buffer& buffer, const KeyChord& chord); // replay-only; live typing bypasses this

    // ---- Insert-mode Ctrl-chords (InsertModeKeymap's action tags) ----
    void DeleteWordBackInInsert(text::Buffer& buffer);
    void DeleteToLineStartInInsert(text::Buffer& buffer);
    void ShiftInsertLine(text::Buffer& buffer, bool more);
    void InsertRegisterAtPoint(text::Buffer& buffer, char32_t name);

    // ---- Normal/Visual grammar helpers ----
    [[nodiscard]] long                        EffectiveCount() const;
    [[nodiscard]] std::optional<char32_t>     ResolveOperatorChord(const KeyChord& chord) const;
    [[nodiscard]] std::optional<MotionResult> TryImmediateMotion(const text::Buffer& buffer, const KeyChord& chord, long count);
    void                                      ResolveMotionAndAct(text::Buffer& buffer, const MotionResult& motion, bool horizontal);
    void                                      ApplyOperatorRange(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t target, bool linewise, bool inclusive);
    void                                      ApplyOperator(text::Buffer& buffer, char32_t op, std::size_t start, std::size_t end, bool linewise);
    void                                      ApplyDoubledOperator(text::Buffer& buffer, char32_t op, long count);
    void                                      ApplyTextObject(text::Buffer& buffer, bool inner, const KeyChord& objectChord);
    void                                      HandleGPrefixed(text::Buffer& buffer, const KeyChord& chord);
    bool                                      HandleVisualSpecific(text::Buffer& buffer, const KeyChord& chord, long count); // true if the chord was consumed
    void                                      HandleAction(text::Buffer& buffer, const KeyChord& chord, long count);

    void ApplyVisualOperator(text::Buffer& buffer, char32_t op);
    void ApplyVisualBlockOperator(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t point);
    void ApplyVisualBlockInsert(text::Buffer& buffer, bool atStart);
    void ApplyVisualBlockChange(text::Buffer& buffer);

    void ShiftLines(text::Buffer& buffer, std::size_t start, std::size_t end, bool more);
    void ToggleCaseRange(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t op);
    void JoinLines(text::Buffer& buffer, long count);

    void BeginInsertSession(text::Buffer& buffer);
    void BeginReplaceSession(text::Buffer& buffer);
    void EnterVisual(text::Buffer& buffer, Mode visualKind);
    void RememberVisualRange(text::Buffer& buffer);
    void PasteRegister(text::Buffer& buffer, bool before, long count);

    void ExecuteExCommand(text::Buffer& buffer, const std::string& text);
    void ExecuteSubstitute(text::Buffer& buffer, const ExCommand& cmd);
    void ExecuteGlobal(text::Buffer& buffer, const ExCommand& cmd);

    void RunSearch(text::Buffer& buffer, bool forward, const std::string& pattern);
    void PerformSearch(text::Buffer& buffer, bool forward, const std::string& pattern);
    void RepeatSearch(text::Buffer& buffer, bool sameDirection);
    void SearchWordUnderPoint(text::Buffer& buffer, bool forward);

    void RepeatLastFind(text::Buffer& buffer, bool sameDirection);
    void RepeatLastChange(text::Buffer& buffer);

    void SetMarkAt(text::Buffer& buffer, char32_t name);
    void GotoMark(text::Buffer& buffer, char32_t name, bool linewise);

    void StartMacroRecording(char32_t name);
    void StopMacroRecording();
    void PlayMacro(text::Buffer& buffer, char32_t name, long count);

    void FinishCommand(text::Buffer& buffer);
    void UpdateGoalColumn(const text::Buffer& buffer);

    Mode        mode_       = Mode::Normal;
    std::size_t goalColumn_ = 0;

    long     countBuffer_         = 0;
    bool     hasCount_            = false;
    char32_t pendingRegisterName_ = 0;

    std::optional<char32_t> pendingOperator_;
    long                    operatorCount_ = 0;

    CharHandler pendingCharHandler_;

    // C-r in Insert mode is a two-step read (the chord itself, then the register name
    // typed right after) but Insert-mode typing has no pendingCharHandler_-style
    // mechanism of its own -- HandleInsertModeChord checks this flag itself instead.
    bool awaitingInsertRegisterName_ = false;

    char32_t lastFindChar_    = 0;
    bool     lastFindForward_ = true;
    bool     lastFindTill_    = false;
    bool     hasLastFind_     = false;

    char32_t    commandLinePrefix_ = 0; // ':' / '/' / '?'
    std::string commandLineText_;

    std::optional<std::string> lastSearchPattern_;
    bool                       lastSearchForward_ = true;

    std::size_t            visualAnchor_ = 0;
    std::optional<ExRange> lastVisualRange_; // '< / '>, remembered when leaving Visual mode
    bool                   blockInsertSession_ = false;

    // gv's own memory -- exact byte offsets and the visual kind, distinct from
    // lastVisualRange_'s line-only shape (which only needs to serve ':<,'>'-style ranges
    // and the linewise '< / '> marks).
    std::size_t lastVisualAnchor_ = 0;
    std::size_t lastVisualPoint_  = 0;
    Mode        lastVisualKind_   = Mode::Visual;
    bool        hasLastVisual_    = false;

    std::map<char32_t, std::size_t> marks_;

    VimRegisters registers_;

    std::vector<KeyChord> currentCommandChords_;
    std::size_t           generationBeforeCommand_ = 0;
    std::vector<KeyChord> lastChange_;
    int                   replayDepth_ = 0; // guards against runaway recursive "."/@ replay

    bool                                      isRecordingMacro_       = false;
    char32_t                                  recordingMacroRegister_ = 0;
    std::vector<KeyChord>                     macroRecordingBuffer_;
    std::map<char32_t, std::vector<KeyChord>> macros_;
    char32_t                                  lastMacroRegister_ = 0;

    std::size_t topLine_        = 0;
    std::size_t viewportHeight_ = 0;

    std::string   statusText_;
    PendingIntent pendingIntent_ = PendingIntent::None;
};

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMENGINE_H
