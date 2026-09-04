//
// Debugging-wishlist follow-up (ROADMAP.md's Maybelist, the Valgrind entry):
// the sibling this project's sanitizer-output-parser deliberately left open
// -- "a structured one: `--xml=yes` memcheck/helgrind output parsed into a
// synthetic diagnostics buffer". Same shape as SanitizerOutputParser.h/
// TestOutputParser.h's own "junit-xml" parser: pure, hand-rolled scanning
// pinned against real captured tool output, no XML library dependency (this
// codebase's own stated precedent -- see TestOutputParser.h's own doc
// comment on why "junit-xml" is a hand-rolled tolerant scanner).
//
// Every `--xml=yes` run of any Valgrind tool (memcheck, helgrind, drd, ...)
// shares the same core XML shape: a top-level `<tool>...</tool>` naming which
// tool produced the report, and zero or more attribute-free `<error>...
// </error>` blocks -- `<kind>`, either `<what>` (a plain message) or
// `<xwhat><text>...</text>...</xwhat>` (memcheck's leak-report shape, which
// also carries `<leakedbytes>`/`<leakedblocks>`, not extracted here), and one
// or more `<stack>` blocks of `<frame>` elements. This parser takes the first
// frame in the error's *first* `<stack>` block that carries both `<file>`
// and `<line>` as the finding's location -- the "what actually happened
// here" site (current access for memcheck, current thread's stack for a
// helgrind race), not a leak's allocation-site stack or a race's "previous
// access" second stack. A frame with no debug info (library code, or the
// interceptor wrapper itself) is skipped in favor of the next one; a frame
// with `<dir>` alongside `<file>` joins them into a full path, matching what
// `BufferView::VisitResultUnderPoint`'s click/Enter-to-source needs.
//
// Every tag this parser looks for (`error`, `kind`, `what`, `xwhat`, `text`,
// `stack`, `frame`, `file`, `dir`, `line`, `tool`) is attribute-free in real
// Valgrind XML output, which is what keeps this a plain nested tag-content
// scan instead of JUnit XML's fuller attribute parser.
//
// Scope, deliberately not chased further: only the first `<stack>` per error
// is walked (a leak's allocation site, or a race's second "previous access"
// stack, are not surfaced as a second location); `<auxwhat>`/`<xauxwhat>`
// supplementary text is not folded into the message; `<leakedbytes>`/
// `<leakedblocks>` aren't surfaced as structured fields.
//

#ifndef NED_EDITOR_VALGRINDOUTPUTPARSER_H
#define NED_EDITOR_VALGRINDOUTPUTPARSER_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct ValgrindFinding {
    std::string tool; // "memcheck", "helgrind", ... verbatim from the document's own
                       // top-level <tool> element; empty if the document carries none.
    std::string kind;    // Verbatim <kind> text -- "InvalidRead", "Leak_DefinitelyLost", "Race", ...
    std::string message; // <xwhat><text> when present (memcheck leak reports), else <what>;
                          // empty if the error block carries neither.
    std::string file;    // <dir>/<file> joined when both present, else bare <file>; empty when no
                          // frame in the error's first <stack> block carries a location at all.
    std::size_t line = 0; // 1-based; 0 = unknown/absent

    [[nodiscard]] bool operator==(const ValgrindFinding&) const = default;
};

// Pure, testable, never throws: scans output for <error> blocks inside a
// Valgrind `--xml=yes` report and returns one ValgrindFinding per block, in
// document order. Output that carries no <error> block at all (plain text,
// an ordinary non-XML tool run, or an XML document with zero reported
// errors) yields an empty vector -- "unrecognized input is not an error",
// the same contract every TestOutputParser.h/SanitizerOutputParser.h parser
// follows.
[[nodiscard]] std::vector<ValgrindFinding> ParseValgrindXml(std::string_view output);

} // namespace ned::editor

#endif // NED_EDITOR_VALGRINDOUTPUTPARSER_H
