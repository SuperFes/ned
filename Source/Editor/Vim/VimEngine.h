//
// The Vim modal state machine -- one per BufferView pane (mirrors PrefixArgumentReader/
// IncrementalSearch's "pure-ish, key-by-key, caller-driven" shape, scaled up to Vim's
// much larger grammar). Never touches anything beyond a Buffer& handed in per call, so
// it stays exactly as UI-free as the rest of Editor/.
//
// Deliberate v1 cuts, documented here once rather than scattered across every method
// that would otherwise need its own caveat: no mark letters beyond a-z/A-Z and '</'> (the
// visual-selection marks) -- a-z stay buffer-scoped (see currentBufferIdentity_'s own
// doc comment), A-Z are real cross-file marks (VimGlobalMarks.h), but this engine has no
// notion of "other buffers" beyond that one seam, so nothing else (registers, macros,
// jumpList_/changeList_) is cross-file; macros are real, editable register text
// (StopMacroRecording/PlayMacro, VimRegisters::SetRaw/Get) but spelled in this
// codebase's own Emacs kbd notation (Editor/Key.h's FormatKeySequence/ParseKeySequence)
// rather than real vim's own <key> bracket notation -- a deliberate reuse of existing,
// already-tested infrastructure over a second codec; search/:s/:g translate vim's own default "magic"
// escaping convention to PCRE2 syntax for the common core (VimMagic.h has the exact rule
// table and its own documented cuts -- \ze, \%[...], mid-pattern \m/\M/\V). Insert-mode
// typing itself is not handled here at all: BufferView
// forwards those keystrokes straight through its ordinary Dispatcher path (self-insert-
// command, auto-pair, snippets, ghost completion all keep working unmodified) and only
// calls RecordInsertKey so "." can replay them later -- HandleKey's own Mode::Insert
// branch (HandleInsertKeyDirectly) is a simplified direct-edit fallback exercised only
// during "."/macro replay, not live typing.
//

#ifndef NED_EDITOR_VIM_VIMENGINE_H
#define NED_EDITOR_VIM_VIMENGINE_H

#include <cstddef>
#include <filesystem>
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

    // Set by zz/zt/zb/C-e/C-y -- an explicit "scroll the viewport to this line" request
    // independent of point, which BufferView must apply (SetTopLine) *before* its own
    // ScrollToShowPoint() call, since that call only nudges topLine_ far enough to keep
    // point visible and would otherwise silently undo an explicit recenter.
    [[nodiscard]] std::optional<std::size_t> TakePendingTopLine();

    // vim-global-marks follow-up: jumping to an uppercase (A-Z) mark set in a different
    // file than the one currently open needs to switch the active buffer -- something
    // this engine, deliberately UI-free, can't do itself (see VimGlobalMarks.h's own doc
    // comment). Set by GotoMark when the resolved global mark's path doesn't match the
    // current buffer's; BufferView must consume this the same way it already does
    // TakePendingIntent -- open (or find) the target file and move point to (line,
    // column) there.
    struct PendingBufferJump {
        std::filesystem::path path;
        std::size_t           line   = 0;
        std::size_t           column = 0;
    };
    [[nodiscard]] std::optional<PendingBufferJump> TakePendingBufferJump();

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
    void BeginOneShotNormal(text::Buffer& buffer); // C-o

    // ---- Normal/Visual grammar helpers ----
    [[nodiscard]] long                        EffectiveCount() const;
    [[nodiscard]] std::optional<char32_t>     ResolveOperatorChord(const KeyChord& chord) const;
    [[nodiscard]] std::optional<MotionResult> TryImmediateMotion(const text::Buffer& buffer, const KeyChord& chord, long count);
    void                                      ResolveMotionAndAct(text::Buffer& buffer, const MotionResult& motion, bool horizontal);
    void                                      ApplyOperatorRange(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t target, bool linewise, bool inclusive);
    void                                      ApplyOperator(text::Buffer& buffer, char32_t op, std::size_t start, std::size_t end, bool linewise);
    void                                      ApplyDoubledOperator(text::Buffer& buffer, char32_t op, long count);
    void                                      ApplyTextObject(text::Buffer& buffer, bool inner, const KeyChord& objectChord);
    [[nodiscard]] ObjectRange                 ResolveTextObjectRange(const text::Buffer& buffer, bool inner, char32_t objectChar, long count) const;
    void                                      HandleGPrefixed(text::Buffer& buffer, const KeyChord& chord);
    void                                      HandleZPrefixed(text::Buffer& buffer, const KeyChord& chord);
    void                                      HandleCapitalZPrefixed(text::Buffer& buffer, const KeyChord& chord);
    bool                                      HandleVisualSpecific(text::Buffer& buffer, const KeyChord& chord, long count); // true if the chord was consumed
    void                                      HandleAction(text::Buffer& buffer, const KeyChord& chord, long count);

    // ds/cs/ys (Editor/Vim/VimSurround.h) -- entered when 's' arrives with pendingOperator_
    // already d/c/y (real vim-surround's own "ys"/"ds"/"cs" two-letter mappings, reusing
    // the ordinary d/c/y operator-pending state this engine already sets up rather than a
    // new prefix key). Sets up the pendingCharHandler_ chain reading whatever the specific
    // form still needs (ds: one char; cs: two; ys: a text object, or a doubled 's' for the
    // current-line form, then one char).
    void BeginSurroundSequence(text::Buffer& buffer, char32_t op);
    void FinishAddSurround(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t to);

    void ApplyVisualOperator(text::Buffer& buffer, char32_t op);
    void ApplyVisualBlockOperator(text::Buffer& buffer, char32_t op, std::size_t anchor, std::size_t point);
    void ApplyVisualBlockInsert(text::Buffer& buffer, bool atStart);
    void ApplyVisualBlockChange(text::Buffer& buffer);

    void ShiftLines(text::Buffer& buffer, std::size_t start, std::size_t end, bool more);
    void ToggleCaseRange(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t op);
    void JoinLines(text::Buffer& buffer, long count, bool insertSpace = true); // gJ passes false

    // The four read-only special registers (., %, :, /) -- intercepted before
    // VimRegisters is ever consulted, since none of them are ordinary named storage:
    // '.' and '%' need state VimRegisters has no access to (last-inserted text, the live
    // Buffer's own path), and while '/' could live in VimRegisters trivially, keeping all
    // four together in one place is simpler than splitting the interception. Falls
    // through to registers_.Get(name) for every other register name.
    [[nodiscard]] std::optional<RegisterEntry> ReadRegister(const text::Buffer& buffer, char32_t name) const;

    void BeginInsertSession(text::Buffer& buffer);
    void BeginReplaceSession(text::Buffer& buffer);
    void EnterVisual(text::Buffer& buffer, Mode visualKind);
    void RememberVisualRange(text::Buffer& buffer);
    void PasteRegister(text::Buffer& buffer, bool before, long count);

    // Shared by PasteRegister's Line-kind branch and :m/:t/:pu's own line insertion --
    // handles the "buffer ends without a trailing newline" edge case identically either
    // way. Sets point to the first pasted line's first non-blank column.
    void InsertLineBlock(text::Buffer& buffer, const std::vector<std::string>& pieces, std::size_t line, bool before);

    void ExecuteExCommand(text::Buffer& buffer, const std::string& text);
    void ExecuteSubstitute(text::Buffer& buffer, const ExCommand& cmd);
    void ExecuteGlobal(text::Buffer& buffer, const ExCommand& cmd);
    void SubstituteLineRange(text::Buffer& buffer, const ExSubstituteArgs& args, std::size_t startLine, std::size_t endLine);
    void ExecuteMoveOrCopy(text::Buffer& buffer, const ExCommand& cmd, bool isMove);
    void ExecuteSort(text::Buffer& buffer, const ExCommand& cmd);
    void ExecuteRead(text::Buffer& buffer, const ExCommand& cmd);

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

    // C-o in Insert mode: execute exactly one Normal-mode command (possibly an
    // operator+motion, not necessarily a single keystroke), then resume Insert. Set by
    // BeginOneShotNormal, consumed by FinishCommand the moment mode_ genuinely returns to
    // Normal on its own (an operator left pending, e.g. "C-o d", correctly keeps this set
    // and mode_ at Normal until the motion completing it arrives). Cleared defensively
    // (without resuming Insert) by BeginInsertSession/BeginReplaceSession/EnterVisual too,
    // since a one-shot command that itself starts a new modal session (an unusual thing
    // to type, e.g. "C-o A" or "C-o v") reaches mode_ != Mode::Normal by a path other than
    // FinishCommand's own resume branch -- left unguarded, the flag would otherwise stay
    // stuck true and incorrectly hijack a later, unrelated command's own FinishCommand
    // call. A documented v1 edge case: such a one-shot command's own dot-repeat/dot
    // register bookkeeping isn't specially unified with the interrupted Insert session's.
    bool oneShotNormalPending_ = false;

    char32_t lastFindChar_    = 0;
    bool     lastFindForward_ = true;
    bool     lastFindTill_    = false;
    bool     hasLastFind_     = false;

    char32_t    commandLinePrefix_ = 0; // ':' / '/' / '?'
    std::string commandLineText_;

    std::optional<std::string> lastSearchPattern_;
    bool                       lastSearchForward_ = true;

    // & (repeat last :s, current line only) -- the parsed args, not the raw text, so it
    // doesn't need to re-run ParseSubstituteArgs itself.
    std::optional<ExSubstituteArgs> lastSubstitute_;

    // The ":" and "." special registers' own backing state.
    std::string lastExCommandText_;
    std::string lastInsertedText_;    // committed at ExitInsertToNormal
    std::string insertModeTypedText_; // accumulates live during a session, see RecordInsertKey

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

    // jumplist-ring follow-up: generalizes kJumpMark's single-slot ``/'' toggle (still
    // unchanged -- see GotoMark) into a real back/forward ring, navigated by C-o/C-i.
    // jumpList_ holds byte offsets in the order they were left; jumpListPos_ is an index
    // into it in [0, jumpList_.size()] where == size() means "at the live position,
    // nothing navigated since the last new jump" (real vim's own jumplist model: the
    // current line is a virtual entry past the end until C-o forces it in). Pushed from
    // every site that already stamps kJumpMark (G, gg, GotoMark, RunSearch -- so /, ?, n,
    // N, *, # all count too, a deliberate simplification vs. real vim, which doesn't
    // treat every n/N repeat as its own jump) via PushJumpListEntry.
    std::vector<std::size_t> jumpList_;
    std::size_t              jumpListPos_ = 0;
    void                     PushJumpListEntry(const text::Buffer& buffer);
    void                     JumpListBack(text::Buffer& buffer);
    void                     JumpListForward(text::Buffer& buffer);

    // changelist-ring follow-up: g;/g, walk changeList_ -- real vim's changelist, a
    // simpler cousin of jumpList_ above. Every entry is already a position an edit
    // actually happened at (unlike jumpList_'s "position being left"), so there's no
    // jumpList_-style "live" out-of-bounds index -- changeListPos_ is always a valid
    // index into changeList_ whenever it's non-empty, reset to the newest entry
    // (size() - 1) on every push. Pushed from FinishCommand, the same site that updates
    // lastChange_ (its own "did content actually change" gate is exactly the granularity
    // real vim's changelist uses too: one entry per whole Insert/Replace/Visual-change
    // session, not per keystroke, since FinishCommand only runs that check once mode_
    // genuinely returns to Normal). Consecutive changes on the same line collapse into
    // one updated entry rather than accumulating, matching real vim's own documented
    // behavior.
    std::vector<std::size_t> changeList_;
    std::size_t              changeListPos_ = 0;
    void                     PushChangeListEntry(text::Buffer& buffer);
    void                     ChangeListOlder(text::Buffer& buffer);
    void                     ChangeListNewer(text::Buffer& buffer);

    VimRegisters registers_;

    std::vector<KeyChord> currentCommandChords_;
    std::size_t           generationBeforeCommand_ = 0;
    std::vector<KeyChord> lastChange_;
    int                   replayDepth_ = 0; // guards against runaway recursive "."/@ replay

    // vim-macro-register follow-up: macros are stored as real register text now
    // (registers_, via VimRegisters::SetRaw/Get) rather than a private cache here --
    // isRecordingMacro_/recordingMacroRegister_/macroRecordingBuffer_ are just the
    // in-flight recording state, not the macro's storage.
    bool                   isRecordingMacro_       = false;
    char32_t               recordingMacroRegister_ = 0;
    std::vector<KeyChord>  macroRecordingBuffer_;
    char32_t               lastMacroRegister_ = 0;

    std::size_t topLine_        = 0;
    std::size_t viewportHeight_ = 0;

    std::optional<std::size_t> pendingTopLine_;

    // gi's own memory: where Insert mode was last exited from (before ExitInsertToNormal's
    // own point-back-one-grapheme adjustment), distinct from wherever point ends up moving
    // to afterward.
    std::size_t lastInsertExitPoint_ = 0;

    std::string   statusText_;
    PendingIntent pendingIntent_ = PendingIntent::None;

    // vim-global-marks follow-up: see TakePendingBufferJump's own doc comment above.
    std::optional<PendingBufferJump> pendingBufferJump_;

    // buffer-scoped-marks follow-up: this engine is one-per-pane (BufferView.h), not
    // one-per-buffer, but real vim's lowercase/''/`` marks (and, unlike real vim,
    // jumpList_/changeList_ too -- see their own doc comments) are meant to be
    // buffer-scoped -- switching which buffer a pane shows (switch-to-buffer, a tab
    // click, ...) previously left them holding stale byte offsets from whatever buffer
    // was active when they were set, silently misapplied to a *different* buffer's
    // content if that buffer happened to be long enough. Noted (and all three cleared)
    // the moment HandleKey sees a different Buffer& than last time -- a raw, non-owning
    // identity pointer is safe here since it's only ever compared against a live Buffer&
    // handed in per call, never dereferenced.
    const text::Buffer* currentBufferIdentity_ = nullptr;
};

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMENGINE_H
