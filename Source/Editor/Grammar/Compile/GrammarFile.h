//
// A language's grammar as data: `Source/Languages/<name>/grammar.janet`,
// read without the Janet VM (Editor/JanetData.h) into the model the table
// compiler consumes. One file per language, alongside its language.janet.
//
// The form set is tree-sitter's rule vocabulary one to one, so a grammar
// imported from a `grammar.json` says exactly what the original said and
// the compiler's semantics can be held against the tables that grammar
// already generated:
//
//   name                      a rule reference (bare symbol)
//   (:ref "name")             the same, for a name Janet would read as
//                             data (true, false, nil)
//   "literal"                 a string token
//   (:pattern "regex" "i")    a regex token, flags optional
//   :blank                    the empty rule
//   (:seq a b ...)  (:choice a b ...)
//   (:repeat a)  (:repeat1 a)
//   (:prec 1 a)  (:prec-left 1 a)  (:prec-right 1 a)  (:prec-dynamic 1 a)
//                             a number, or a "name" from :precedences
//   (:token a)  (:token-immediate a)
//   (:alias a name)           name a symbol = a named node, a "string" = an
//                             anonymous one
//   (:field :name a)
//   (:reserved :context a)    a reserved-word context from :reserved
//
// The file is one struct: :name, :word, :extras, :conflicts, :precedences,
// :externals, :inline, :supertypes, :reserved and :rules (a struct of
// name -> rule, in order -- the first rule is the start rule and the order
// is the symbol numbering).
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_GRAMMARFILE_H
#define NED_EDITOR_GRAMMAR_COMPILE_GRAMMARFILE_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::grammar::compile {

struct GrammarFile {
    struct Rule {
        enum class Kind {
            Blank,
            Symbol,
            String,
            Pattern,
            Seq,
            Choice,
            Repeat,
            Repeat1,
            Prec,
            PrecLeft,
            PrecRight,
            PrecDynamic,
            Token,
            TokenImmediate,
            Alias,
            Field,
            Reserved,
        };
        Kind              kind = Kind::Blank;
        std::string       text;           // Symbol: rule name; String: literal; Pattern: regex; Alias: new name;
                                          // Field: field name; Reserved: context name
        std::string       flags;          // Pattern
        int               precedence = 0; // Prec*: numeric value, when precedenceName is empty
        std::string       precedenceName; // Prec*: a named precedence
        bool              named = false;  // Alias
        std::vector<Rule> children;       // Seq/Choice: members; every other wrapper: exactly one

        bool operator==(const Rule&) const = default;

        [[nodiscard]] const Rule& Child() const {
            return children.front();
        }
    };

    std::string name;
    std::string word;     // empty = no keyword extraction
    std::string inherits; // provenance only: the grammar this one was derived from

    std::vector<std::pair<std::string, Rule>>              rules;
    std::vector<Rule>                                      extras;
    std::vector<Rule>                                      externals;
    std::vector<std::vector<std::string>>                  conflicts;
    std::vector<std::vector<Rule>>                         precedences; // Symbol or String items
    std::vector<std::string>                               inlineRules;
    std::vector<std::string>                               supertypes;
    std::vector<std::pair<std::string, std::vector<Rule>>> reserved; // context -> word rules

    bool operator==(const GrammarFile&) const = default;
};

class GrammarFileError : public std::runtime_error {
  public:
    GrammarFileError(int line, const std::string& message);
    [[nodiscard]] int Line() const {
        return line_;
    }

  private:
    int line_;
};

// From a tree-sitter grammar.json. Ordered, because the object order of
// "rules" (and "reserved") is meaning, not layout. Throws GrammarFileError
// (line 0) on a rule type this vocabulary lacks.
[[nodiscard]] GrammarFile ParseGrammarJson(const nlohmann::ordered_json& grammar);

// From grammar.janet text. Throws GrammarFileError with the offending line.
[[nodiscard]] GrammarFile ParseGrammarJanet(std::string_view source);

// grammar.janet text; ParseGrammarJanet reads it back to an equal model.
[[nodiscard]] std::string ToGrammarJanet(const GrammarFile& grammar);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_GRAMMARFILE_H
