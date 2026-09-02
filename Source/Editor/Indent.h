//
// smart-indentation follow-up. The generic, tree-sitter-query-driven indent
// engine -- the same "one generic engine + N per-language query files as
// data" shape CodeFold.h/Injection.h already establish for fold/injection
// regions, applied to Mode::indentColumn. A per-language indents.scm supplies
// "indent"/"dedent"-named captures (nvim-treesitter/Helix's own capture-name
// convention, borrowed as names only -- NOT their #set!-based priority/scope
// directives, which Query::Captures() never evaluates at all; see Query.h's
// own doc comment on why an unrecognized predicate/directive is inert).
//
// BuildIndentFunction below is what TreeSitterModeFromLanguage (Mode.cpp)
// calls to construct Mode::indentColumn for most bundled modes, mirroring
// FoldFunction/testDiscovery's own construction exactly. Not every mode goes
// through this engine -- Markdown's own indentColumn closure is hand-rolled
// directly in MarkdownMode() (mirroring that Mode's already-hand-rolled
// .highlight), because real Markdown list-item continuation needs a hanging
// indent to the bullet's own content COLUMN (e.g. "1. " = 3, "- " = 2), not a
// multiple of one fixed indent width -- a genuinely different shape than
// this engine's level-counting model. Both shapes return the identical
// IndentFunction contract (Mode.h), so every caller here treats them
// identically.
//
// ComputeIndentColumn/IndentRegion/IndentBuffer below are the pure,
// buffer-manipulation half: deliberately the SAME primitives whether called
// interactively (indent-for-tab-command/newline, Commands.cpp) or in batch
// (indent-region/indent-buffer below) -- this is what lets a future
// save-time cleanup pass (Editor/FormatOnSave.h is the natural hook point;
// not built here) reuse IndentRegion/IndentBuffer directly rather than a
// second implementation.
//

#ifndef NED_EDITOR_INDENT_H
#define NED_EDITOR_INDENT_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "IndentStyle.h"
#include "Mode.h"
#include "Text/Buffer.h"
#include "Text/ITextStorage.h"
#include "TreeSitter/IncrementalParse.h"
#include "TreeSitter/Parser.h"
#include "TreeSitter/Query.h"
#include "TreeSitter/Tree.h"

namespace ned::editor {

// Builds an IndentFunction from a compiled indents.scm-shaped query, sharing
// parser/sharedParse with TreeSitterModeFromLanguage's other closures (Mode.cpp)
// -- not meant to be called directly outside that construction path. modeName
// is the owning Mode's own .name (e.g. "python-mode"), captured by value and
// used to look up IndentStyle::EffectiveIndentStyle fresh on every call (not
// resolved once at construction time), so a live ned/set-indent-style change
// takes effect immediately, the same "read fresh each use" convention
// TabWidth() already follows.
[[nodiscard]] IndentFunction BuildIndentFunction(std::shared_ptr<treesitter::Parser>              parser,
                                                 std::shared_ptr<treesitter::Query>               indentQuery,
                                                 std::shared_ptr<treesitter::IncrementalParseCache> sharedParse,
                                                 std::string                                       modeName);

// The pure per-line tree-walk primitive BuildIndentFunction's closure calls,
// exposed standalone so it can be unit-tested directly against a hand-built
// query/tree without going through a whole *Mode() factory (Tests/
// IndentEngineTest.cpp). Returns std::nullopt only when tree.IsNull() (no
// parse at all) -- a real tree with zero matching captures anywhere returns
// level 0, not nullopt (a mode with an indents.scm but no query match for
// this particular line still has an opinion: "no indent").
//
// Captures partition into "indent" (an ancestor whose own line, when
// distinct from whatever line was last counted, adds one indent level) and
// "dedent" (typically an anonymous closing-delimiter token on the target
// line itself, which instead makes the WHOLE line align with its own
// opener's line rather than one level deeper). See Indent.cpp for the walk
// itself -- the short version: seed a (node, lastRow) pair (either the
// target line's own innermost containing node, or -- when a dedent capture
// starts on this line -- the matching opener's own innermost containing
// node), then walk ancestors counting each "indent"-captured node whose
// StartRow() differs from whatever row was last counted.
[[nodiscard]] std::optional<int> IndentLevelForLine(const treesitter::Tree& tree, std::string_view bufferText,
                                                    const treesitter::Query& indentQuery, std::size_t lineStart,
                                                    std::size_t lineEnd);

// level * style.width -- the only place an abstract indent level ever
// becomes a real visual column.
[[nodiscard]] int IndentColumnForLevel(int level, const IndentStyle& style);

// -- Buffer-level indent-manipulation utilities. None of these exist
// anywhere in the codebase today (confirmed by repo-wide grep) -- back-to-
// indentation/delete-indentation (Commands.cpp) each currently hand-roll an
// equivalent leading-whitespace scan inline; these give that scan one
// shared, tested home rather than a third inline copy.

// Byte offset of the first non-space/non-tab codepoint on the line starting
// at lineStart, or the byte offset of that line's own trailing newline (or
// end of buffer) if the whole line is blank -- i.e. always the END of the
// line's leading whitespace run, whether or not real content follows.
[[nodiscard]] std::size_t LineIndentEnd(const text::ITextStorage& content, std::size_t lineStart);

// column spaces, or (if style.useTabs) floor(column / style.width) literal
// tabs followed by (column % style.width) spaces -- the standard "smart
// tabs" mixed rendering, not "always N raw tab bytes regardless of
// remainder." Pure, no Buffer dependency. column <= 0 returns "".
[[nodiscard]] std::string IndentString(int column, const IndentStyle& style);

// Replaces [lineStart, LineIndentEnd(...)) with IndentString(column, style)
// as one undo step (Buffer::BeginUndoGroup/EndUndoGroup around a single
// DeleteRange+InsertAt pair) -- a genuine no-op (the existing run already
// matches) touches the buffer/undo tree not at all. Returns the signed
// byte-length delta (insertedLength - deletedLength), 0 for a no-op -- what
// a caller adjusting subsequent cached byte offsets after this edit needs.
std::ptrdiff_t SetLineIndent(text::Buffer& buffer, std::size_t lineStart, int column, const IndentStyle& style);

// -- Batch reindent (the linter/format-on-save-reuse requirement -- see this
// file's own header comment). Walks [startLine, endLineExclusive)
// BOTTOM-TO-TOP, deliberately: editing a later line's own leading whitespace
// never invalidates an earlier, still-to-process line's byte offsets, so no
// re-derivation pass is needed between lines. The whole range is one undo
// step (an outer BeginUndoGroup/EndUndoGroup wrapping every per-line
// SetLineIndent's own inner group). No-op (returns 0, touches nothing) if
// mode.indentColumn is unset. Returns the number of lines actually changed.
//
// Known v1 cost, accepted rather than engineered around now (mirrors
// Buffer.cpp's own kMaxTabAwareColumnScan precedent of "bounded/approximate
// over unbounded/exact"): buffer.Text() -- a full O(n) materialize -- is
// called once per line processed, so this is O(n * linesInRange) for a large
// n. Fine for an occasional, user-triggered whole-buffer cleanup (the same
// cost class as fill-paragraph or a manual reindent, not a hot per-frame
// path); a future optimization is a real follow-up only if this proves slow
// on a genuinely huge file in practice, not built speculatively here.
std::size_t IndentRegion(text::Buffer& buffer, const Mode& mode, std::size_t startLine, std::size_t endLineExclusive);

// IndentRegion(buffer, mode, 0, buffer.Content().LineCount()).
std::size_t IndentBuffer(text::Buffer& buffer, const Mode& mode);

} // namespace ned::editor

#endif // NED_EDITOR_INDENT_H
