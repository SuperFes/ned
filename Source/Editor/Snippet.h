//
// TextMate-style snippet expansion: a registered trigger word (see
// SnippetRegistry.h) expands into a skeleton with numbered fill-in fields the
// user TABs between. This file is the pure parsing half -- string in,
// stripped text + field byte offsets out -- mirroring OrgCapture.h's own
// "pure expansion, then a thin Buffer-mutating layer" split.
//
// Supported syntax (the TextMate/LSP snippet grammar's core): `$1`, `${1}`,
// `${1:placeholder}`, `$0` (the final stop -- point lands there when the
// session ends; an implicit zero-length `$0` is appended at end-of-text when
// the body has none), and mirrors (the same index appearing more than once --
// every occurrence is substituted with the index's placeholder text, and a
// live session propagates typing across them). Escapes: `\$` and `\\`
// anywhere, plus `\}` inside a placeholder. Anything ill-formed (`${`,
// `${x:`, an unterminated placeholder) is passed through as literal text,
// never an error -- LSP servers send this syntax and a bad body must degrade
// to visible text, not a refusal.
//
// snippet-expansion-gaps follow-up: `$TM_*`/other editor-context variables,
// `${1|a,b|}` choices, and `${1/regex/format/flags}` tabstop transforms are
// all real support now -- see SnippetVariables/SnippetTransform below and
// TryParseVariable/ParseTransformSuffix in the .cpp. A nested placeholder
// (`${1:foo ${2:bar}}`) still contributes only its literal text ("foo bar")
// with the inner tabstop dropped, and a variable reference nested inside
// another placeholder's own default text doesn't resolve (only a top-level
// `$`/`${` reference does) -- both stay documented v1 cuts: overlapping
// field ranges are a relocation-semantics can of worms, and the nested-
// variable case is rare enough not to be worth it either.
//

#ifndef NED_EDITOR_SNIPPET_H
#define NED_EDITOR_SNIPPET_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

// $TM_*/CLIPBOARD/CURRENT_*/etc. editor-context values a caller resolves
// once, before parsing -- ParseSnippet itself has no buffer/filesystem
// access, matching this file's "pure parsing" positioning. An empty field
// is treated as "unset" (falls back to the variable's own `${VAR:default}`
// clause when the body supplies one, else ""); RANDOM/RANDOM_HEX/UUID are
// the one genuinely nondeterministic exception and are generated directly
// in Snippet.cpp rather than threaded through here. See Snippet.cpp's
// LookupKnownVariable for the exact supported name list.
struct SnippetVariables {
    std::string selectedText;                            // TM_SELECTED_TEXT
    std::string currentLine;                             // TM_CURRENT_LINE
    std::string lineNumber;                              // TM_LINE_NUMBER (1-based)
    std::string lineIndex;                               // TM_LINE_INDEX (0-based)
    std::string filename;                                // TM_FILENAME
    std::string filenameBase;                            // TM_FILENAME_BASE
    std::string directory;                               // TM_DIRECTORY
    std::string filepath;                                // TM_FILEPATH
    std::string relativeFilepath;                        // RELATIVE_FILEPATH (relative to ProjectRoot())
    std::string clipboard;                               // CLIPBOARD
    std::string year, month, date, hour, minute, second; // CURRENT_YEAR/MONTH/DATE/HOUR/MINUTE/SECOND
};

// A tabstop mirror's live regex transform (`${1/regex/format/flags}` -- the
// LSP snippet grammar's own transform production, VSCode's `${n:/upcase}`-
// style format mini-language, not TextMate's older `\U...\E` convention).
// Recomputed from the primary tabstop's current content on every
// SnippetSession::SyncMirrors call (and once, against the primary's initial
// text, by ParseSnippet itself) -- never diffed/cached, the same
// "recompute don't diff" shape ContentGeneration()-gated caches elsewhere in
// this codebase use for a much larger cost, unneeded here since a single
// field's content is tiny.
struct SnippetTransform {
    std::string pattern;        // PCRE2 source; "(?i)" already prefixed when the /i flag was present
    std::string format;         // the format mini-language string, applied per match
    bool        global = false; // the /g flag -- every match transformed, not just the first
};

// One field occurrence in ParsedSnippet::text. index 0 is the final stop.
struct SnippetField {
    int         index;
    std::size_t start; // byte offsets into ParsedSnippet::text; start <= end
    std::size_t end;
    // Set only on an occurrence that was itself written as
    // `${N/regex/format/flags}` -- a plain mirror (bare `$N`/`${N}`) or the
    // placeholder-carrying primary never carries one.
    std::optional<SnippetTransform> transform;
};

// fields is ordered by visit order -- ascending index with 0 sorted last --
// and, within one index, the primary occurrence (the first one whose own
// syntax carried a placeholder, else the first occurrence) ahead of its
// mirrors in document order. A session leans on that contract: "the first
// field listed for an index" is where point lands when the index is visited.
struct ParsedSnippet {
    std::string               text;   // body with all markers stripped, placeholders substituted
    std::vector<SnippetField> fields; // every occurrence, mirrors included; never empty (implicit $0)
};

[[nodiscard]] ParsedSnippet ParseSnippet(std::string_view body, const SnippetVariables& variables = {});

// One live snippet-expansion session over one buffer -- the state machine
// BufferView drives key-by-key, mirroring IncrementalSearch/
// PrefixArgumentReader's "pure Editor-layer state machine, no UI types"
// shape. The buffer is passed into every call rather than held: the owning
// BufferView re-resolves it by name (BufferName()) per keystroke, so a
// buffer closed mid-session is a safe no-op there, never a dangling
// reference here. Field positions live in the buffer itself
// (Buffer::SnippetRanges, relocated across every edit); this class holds
// only the visit order and which field is active. Every mutating method
// leaves undo grouping to the caller except Start (which wraps its own
// delete-trigger + insert-skeleton in one group so the whole expansion is
// one undo step).
class SnippetSession {
  public:
    // Performs the expansion: replaces [replaceStart, replaceEnd) (the
    // trigger word, or a completion's typed prefix) with parsed.text as one
    // undo step, registers parsed.fields as the buffer's snippet ranges,
    // and activates the first field (point at its end; a non-empty
    // placeholder starts Pristine -- see below). Returns nullopt -- with
    // the text still inserted and point at the final stop -- when the
    // snippet has no real fields, i.e. only its $0: no session is worth
    // holding for a body with nothing to hop between.
    [[nodiscard]] static std::optional<SnippetSession> Start(text::Buffer& buffer, std::string bufferName,
                                                             std::size_t replaceStart, std::size_t replaceEnd,
                                                             const ParsedSnippet& parsed);

    enum class NavResult {
        Moved,
        Finished // landed on the final stop -- caller ends the session
    };
    // Advances to the next/previous tabstop index (a mirror group is one
    // stop -- point lands in its primary field). NextField past the last
    // real field places point at $0 and reports Finished; PreviousField at
    // the first field stays put.
    NavResult NextField(text::Buffer& buffer);
    NavResult PreviousField(text::Buffer& buffer);
    // Ends the session's buffer-side state (clears the ranges); the
    // expanded text stays.
    void Finish(text::Buffer& buffer);

    // A just-entered field with placeholder text is "pristine": the first
    // plain typed character replaces the whole placeholder (the caller
    // deletes via DeleteActiveFieldContent before dispatching the
    // keystroke), Backspace deletes it outright, and any other key clears
    // the flag and edits normally.
    [[nodiscard]] bool Pristine() const;
    void               ClearPristine();
    void               DeleteActiveFieldContent(text::Buffer& buffer);

    // Propagates the active field's current content to its mirrors (same
    // tabstop index). No-op when nothing changed since the last sync; each
    // mirror rewrite is a real DeleteRange+InsertAt, re-reading the
    // buffer's ranges between edits (every edit relocates every other
    // range) and repairing the rewritten mirror via UpdateSnippetRange.
    // Caller owns undo grouping (one group per keystroke wraps the
    // dispatched edit and this sync together).
    void SyncMirrors(text::Buffer& buffer);

    // False once the buffer's snippet ranges are gone (undo/redo cleared
    // them, or content shrank past them) -- the caller's cue to end.
    [[nodiscard]] bool RangesValid(const text::Buffer& buffer) const;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
                                     ActiveFieldRange(const text::Buffer& buffer) const;
    [[nodiscard]] const std::string& BufferName() const;
    [[nodiscard]] std::string        StatusText() const;

  private:
    SnippetSession() = default;

    // Enters visitOrder_[activePos_]: marks its primary range active, puts
    // point at the field's end, arms Pristine for a non-empty field.
    void                                            EnterActiveField(text::Buffer& buffer);
    [[nodiscard]] const text::Buffer::SnippetRange* FindRange(const text::Buffer& buffer,
                                                              std::size_t         id) const;

    std::string      bufferName_;
    std::vector<int> visitOrder_; // distinct tabstop indices, ascending, 0 last
    std::size_t      activePos_     = 0;
    std::size_t      activeRangeId_ = 0; // the active index's primary range
    bool             pristine_      = false;
    std::string      lastSyncedText_; // active primary's content as of the last sync
    // Range id -> its own live transform, populated from ParsedSnippet's
    // fields at Start() time. Only a mirror's id is ever looked up (see
    // SyncMirrors), but populated for every id that has one regardless of
    // whether it turned out to be the primary -- harmless, since the
    // primary's own id is never looked up here.
    std::unordered_map<std::size_t, SnippetTransform> transforms_;
};

} // namespace ned::editor

#endif // NED_EDITOR_SNIPPET_H
