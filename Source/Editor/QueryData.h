//
// Tree-sitter query patterns as data. A query file in this codebase is Janet
// reader syntax -- the same S-expressions tree-sitter's own query language
// is, read as data and never evaluated -- with two spellings that differ:
// a comment is `#` to end of line (tree-sitter: `;`), and a predicate or
// directive is keyword-headed, `(:eq? @x "foo")` / `(:set! k "v")`
// (tree-sitter: `#eq?`, which is a comment marker to Janet). Everything else
// -- `(node)`, `field:`, `@capture`, `[alternation]`, `.` anchors, `!field`,
// `_`, `*`/`+`/`?` quantifiers, "strings" -- reads identically in both.
//
// Two readers, one data model, one writer. ParseJanet is what every language
// loads through; ParseScm exists to convert upstream grammars' files (the
// bless step in Tests/QueryDataTest.cpp) and to read a system tree-sitter
// install's queries for a runtime-loaded grammar. ToQueryText emits what
// ts_query_new consumes -- the one place tree-sitter's syntax is still
// produced, and an implementation detail of the tree-sitter backend rather
// than a format anyone authors.
//

#ifndef NED_EDITOR_QUERYDATA_H
#define NED_EDITOR_QUERYDATA_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::querydata {

struct Form {
    enum class Kind {
        List,        // (node ...) -- items are its children
        Alternation, // [a b] -- items are the alternatives
        Symbol,      // node name, "field:", "@capture", ".", "!field", "_"
        String,      // "literal", decoded
        Predicate,   // (:eq? ...) / (#eq? ...) -- text is the name without its sigil, items the arguments
        Comment,     // text is the comment body after its marker
    };
    Kind              kind = Kind::Symbol;
    std::string       text;
    char              quantifier = 0; // '*', '+', '?' or none; on a List, Alternation, String or wildcard
    std::vector<Form> items;
    int               line = 0; // 1-based source line the form starts on

    [[nodiscard]] bool operator==(const Form& other) const = default;
};

class QueryDataError : public std::runtime_error {
  public:
    QueryDataError(int line, int column, const std::string& message);
    [[nodiscard]] int Line() const {
        return line_;
    }
    [[nodiscard]] int Column() const {
        return column_;
    }

  private:
    int line_;
    int column_;
};

// The Janet-syntax reader. Rejects what a query never contains and Janet
// would misread -- a stray `;` (a leftover tree-sitter comment, which Janet
// reads as splice), quote/quasiquote/unquote, long strings -- so an
// unconverted file fails loudly rather than loading as the wrong thing.
[[nodiscard]] std::vector<Form> ParseJanet(std::string_view source);

// tree-sitter's own syntax.
[[nodiscard]] std::vector<Form> ParseScm(std::string_view source);

// Emits tree-sitter query syntax. With `preserveLines`, every form is placed
// on the source line it came from (comments become blank lines), so a byte
// offset in a tree-sitter error maps back to a line of the file it was read
// from; without it, one form per line.
[[nodiscard]] std::string ToQueryText(const std::vector<Form>& forms, bool preserveLines = true);

// Textual conversion of a tree-sitter query file to this codebase's Janet
// spelling, byte for byte outside the three things that differ (comment
// marker, predicate sigil, string escapes) -- layout and every comment
// survive, which is what makes a converted upstream file diffable against
// its next version. String escapes are decoded by tree-sitter's own rules
// (`\n`/`\r`/`\t`/`\0`, and any other escaped byte is that byte -- so an
// upstream `"[A-Z\d_]"` really matches a literal `d`) and re-encoded as
// Janet reads them.
[[nodiscard]] std::string ConvertScmToJanet(std::string_view scm);

// Line of a byte offset in `text` (1-based), for mapping a tree-sitter
// error back through ToQueryText's line-preserving output.
[[nodiscard]] int LineOfOffset(std::string_view text, std::size_t offset);

} // namespace ned::editor::querydata

#endif // NED_EDITOR_QUERYDATA_H
