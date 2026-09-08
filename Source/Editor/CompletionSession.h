//
// completion-fidelity follow-up. The live state behind a shown completion
// popup: the candidate set, each candidate's own replace range, the
// selection, and the decision -- on every keystroke -- of whether the set
// can be narrowed locally or the server has to be asked again.
//
// Pure and UI-free, the same shape IncrementalSearch/QueryReplace/
// PrefixArgumentReader/SnippetSession already establish: BufferView owns the
// I/O half (issuing the request, the debounce timer, painting the popup,
// applying the accepted edit), this owns every decision that doesn't need a
// terminal. That split is what makes the interesting logic here -- ranking,
// the narrow-vs-re-request rule, replace-range resolution -- testable
// against a plain Buffer with no Screen, LspClient or subprocess involved.
//
// Two invariants worth stating up front, because most of this file leans on
// them:
//
//   1. A candidate's replace region is always [replaceStart, point). The
//      server hands over an absolute range, but its *end* is only ever the
//      cursor at request time (see kUseInsertRangeForInsertReplace in
//      LspContent.cpp), and typing happens at the cursor -- so re-reading
//      the end from the live point keeps a range valid across the very
//      keystrokes this session exists to survive, with no re-resolution and
//      no stale-position bookkeeping anywhere.
//   2. replaceStart is resolved to a byte offset exactly once, at receipt.
//      It sits at or before point and nothing edits behind the cursor while
//      a popup is up, so it needs no relocation (unlike, say,
//      Buffer::SnippetRange, which does).
//

#ifndef NED_EDITOR_COMPLETIONSESSION_H
#define NED_EDITOR_COMPLETIONSESSION_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Lsp/LspContent.h"

namespace ned::text {
class ITextStorage;
} // namespace ned::text

namespace ned::editor {

// One candidate, resolved against the buffer as it was when the response
// arrived. Deliberately a wrapper around the LSP wire struct rather than a
// replacement for it: the fallback sources (dabbrev, Janet bindings) already
// synthesize CompletionItems, so keeping that type as the payload means this
// session serves all three sources with no conversion. A genuinely
// source-neutral candidate type is a bigger piece of work -- see ROADMAP's
// "merging non-LSP candidates into the same popup" entry, which this makes
// cheaper but does not do.
struct CompletionCandidate {
    lsp::CompletionItem item;
    // Byte offset where accepting this item starts replacing. From the
    // item's own textEdit range when the server sent one, else the caller's
    // word-boundary prefix start (see the constructor).
    std::size_t replaceStart = 0;
    // completion-resolve follow-up: this candidate's index in the session's
    // own unranked master list. candidates_ is a ranked *copy* of that list
    // and is rebuilt from scratch on every keystroke, so a resolve response
    // landing mid-typing has to write through to the master too or the
    // fetched documentation would vanish on the very next character typed.
    // Stable for the session's lifetime; a plain index rather than a pointer
    // because both vectors reallocate.
    std::size_t sourceIndex = 0;
    // Whether completionItem/resolve has already answered for this item --
    // what keeps the debounced resolve from re-asking for a row the user
    // keeps cycling back to. Set even when the response merged nothing (a
    // server with genuinely no extra detail): "asked, don't ask again" is
    // the useful state, not "has documentation."
    bool resolved = false;
};

class CompletionSession {
  public:
    // What a keystroke means for a live session.
    enum class Outcome {
        Keep,       // narrowed locally; the popup stays up, no request needed
        Rerequest,  // the server has to answer again (incomplete list, or a new trigger context)
        Dismiss,    // nothing matches any more, or point left the region this session covers
    };

    // items are taken in server order. isIncomplete is the CompletionList's
    // own flag -- true means the server truncated/approximated the list for
    // this prefix and must be re-asked as it narrows, which is the single
    // input that decides Keep vs. Rerequest below.
    //
    // fallbackPrefixStart is the caller's own word-boundary rule applied at
    // point (WordPrefixStart for LSP/dabbrev items, JanetSymbolPrefixStart
    // for "ned/*" binding items -- the two disagree, which is why it's
    // passed in rather than recomputed here). It supplies replaceStart for
    // any item the server gave no textEdit for, and independently anchors
    // the typed prefix that Refilter matches against.
    CompletionSession(std::vector<lsp::CompletionItem> items, bool isIncomplete, const text::ITextStorage& content,
                      std::size_t point, std::size_t fallbackPrefixStart);

    // The current, ranked candidate set -- empty only if the caller built a
    // session from an empty item list (Refilter never leaves a session empty
    // and Keep at the same time; it reports Dismiss instead).
    [[nodiscard]] const std::vector<CompletionCandidate>& Candidates() const { return candidates_; }
    [[nodiscard]] bool                                    Empty() const { return candidates_.empty(); }
    [[nodiscard]] std::size_t                             SelectedIndex() const { return selectedIndex_; }

    // Out-of-range indices are ignored rather than clamped -- a click racing
    // a just-narrowed list is the real case, and silently selecting a
    // neighbor would accept the wrong item.
    void Select(std::size_t index);
    // Wraps in both directions, matching the popup's existing cycling.
    void Cycle(int direction);

    // Re-evaluates the session against the buffer's current state after an
    // edit at point. currentPrefixStart is the caller's own word-boundary
    // rule applied at the *new* point -- passed rather than recomputed for
    // the same reason the constructor takes one, and because comparing it
    // against the session's fixed prefixStart_ is exactly the "is point
    // still inside the word this session was requested for" test: typing a
    // non-word character (".", "(", whitespace, ...) moves it, deleting past
    // the word's start moves it, and ordinary typing/backspacing within the
    // word does not.
    //
    // Only Dismiss invalidates the session. On Keep *and* Rerequest the
    // candidate set is left narrowed, ranked and displayable -- Rerequest
    // additionally means "ask the server again and replace me when it
    // answers", so the popup keeps showing the locally-narrowed list in the
    // meantime instead of blinking out for the duration of the round trip.
    [[nodiscard]] Outcome Refilter(const text::ITextStorage& content, std::size_t point, std::size_t currentPrefixStart);

    // What accepting the selected candidate does to the buffer: replace
    // [replaceStart, replaceEnd) with newText. replaceEnd is always the
    // live point (invariant 1 above). nullopt when there's no selection to
    // accept.
    //
    // isSnippet items carry their raw TextMate body as newText -- the caller
    // must expand rather than insert it (Editor/Snippet.h), exactly as
    // before; this type reports the range either way so both paths share one
    // resolution rule.
    struct AcceptPlan {
        std::size_t replaceStart = 0;
        std::size_t replaceEnd   = 0;
        std::string newText;
        bool        isSnippet = false;
        // completion-resolve follow-up: the item's additionalTextEdits --
        // the "#include <vector>" an accepted std::vector needs. Positions,
        // not byte offsets, because these are resolved against the buffer at
        // apply time by the caller (Editor/Lsp/LspEditApply.h), which is
        // also what relocates replaceStart/replaceEnd if one of these lands
        // before them. Empty unless a resolve response filled them in.
        std::vector<lsp::WorkspaceTextEdit> additionalEdits;
    };
    [[nodiscard]] std::optional<AcceptPlan> PlanAccept(std::size_t point) const;

    // completion-resolve follow-up. Merges a completionItem/resolve response
    // into the candidate at `index` (an index into Candidates(), i.e. the
    // ranked list the popup shows) and into its master-list twin, and marks
    // it resolved either way. Out-of-range indices are ignored, the same way
    // Select's are and for the same reason: a response landing after the
    // list narrowed under it must not silently write into a neighbor.
    //
    // Only documentation/detail/additionalTextEdits are taken, and only when
    // the resolved item actually carries them -- the LSP spec forbids a
    // server changing an item's insert behavior on resolve, and honoring
    // that is what keeps a response landing mid-typing from rewriting what
    // Tab would insert. label/sortText/filterText are likewise left alone so
    // a resolve can never reorder the list under the user.
    void ApplyResolution(std::size_t index, const lsp::CompletionItem& resolved);

    // completion-trigger-characters follow-up. Whether `ch` (one typed
    // character, UTF-8) is a commit character for the *selected* candidate:
    // typing it should accept that candidate and then insert the character
    // itself. False when there's no selection, or when the selected item
    // declared none -- this session never substitutes a default set, so a
    // server that declares nothing behaves exactly as it did before commit
    // characters were parsed at all.
    [[nodiscard]] bool IsCommitCharacter(std::string_view ch) const;

  private:
    // Ranks allCandidates_ against the typed prefix into candidates_, and
    // resets the selection. Shared by the constructor and Refilter so the
    // initial list and every narrowed one are ordered by the exact same
    // rule.
    //
    // completion-trigger-characters follow-up: for an *empty* prefix the
    // reset selection honors the server's own preselect flag rather than
    // landing on row 0 -- that flag is the server saying "this is the one,"
    // and with nothing typed there is no better evidence. Once a prefix
    // exists the fuzzy ranking is better evidence, so preselect is ignored
    // from then on (VS Code's own rule, and it avoids the popup selecting a
    // row the typed text scores worst).
    void Rank(std::string_view prefix);

    // Every candidate the server sent, untouched -- candidates_ is a ranked
    // subset of this. Kept so a *widening* edit (backspace) can recover
    // candidates a previous narrowing dropped, without a round trip.
    std::vector<CompletionCandidate> allCandidates_;
    std::vector<CompletionCandidate> candidates_;
    std::size_t                      selectedIndex_ = 0;
    bool                             isIncomplete_  = false;
    // Where the typed prefix begins. Fixed for the session's lifetime: it's
    // the floor a backspace may widen back to, and going below it means the
    // user left the word this session was requested for (Dismiss).
    std::size_t prefixStart_ = 0;
};

} // namespace ned::editor

#endif // NED_EDITOR_COMPLETIONSESSION_H
