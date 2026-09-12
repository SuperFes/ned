//
// A reader for Janet's *data* syntax -- structs, tuples, keywords, strings,
// booleans -- with no Janet VM behind it. What `language.janet` is read
// with: a language definition is pure data by design (Docs/ParsingEngine.md,
// "keep the declarative tier pure data"), and reading it without the VM is
// not just principle -- Mode construction runs on ModePrewarm's background
// thread, and Janet itself is main-thread-only, so a definition that needed
// evaluation to load could never be rebuilt where modes are actually built.
// An escape *implementation* is Janet code, but a definition only ever
// *names* one.
//
// The accepted subset is deliberately small: `{...}` structs, `[...]`/`(...)`
// tuples, `:keywords`, `"strings"` (Janet's escape rules, matching
// QueryData.cpp's Janet dialect), `true`/`false`/`nil`, bare symbols, and
// `#` line comments. Anything else -- quote, splice, long strings, a
// function call -- is a loud error, so a definition that has grown code gets
// rejected here rather than silently misread.
//

#ifndef NED_EDITOR_JANETDATA_H
#define NED_EDITOR_JANETDATA_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::janetdata {

struct Value {
    enum class Kind {
        Struct,  // {:k v ...} -- pairs, in source order
        Tuple,   // [...] or (...) -- items
        String,  // decoded
        Keyword, // text without the leading ':'
        Symbol,  // a bare word
        Bool,    // true/false
        Nil,
    };
    Kind        kind = Kind::Nil;
    std::string text;            // String/Keyword/Symbol
    bool        boolean = false; // Bool
    // Struct entries flattened as key, value, key, value... (std::pair of an
    // incomplete type is ill-formed; a flat vector of the type itself is
    // fine) -- Entries() walks them two at a time. Tuple items are `items`.
    std::vector<Value> pairs;
    std::vector<Value> items;
    int                line = 0;

    [[nodiscard]] bool IsStruct() const {
        return kind == Kind::Struct;
    }
    [[nodiscard]] bool IsTuple() const {
        return kind == Kind::Tuple;
    }
    [[nodiscard]] bool IsString() const {
        return kind == Kind::String;
    }
    [[nodiscard]] bool IsKeyword() const {
        return kind == Kind::Keyword;
    }
    [[nodiscard]] bool IsBool() const {
        return kind == Kind::Bool;
    }

    // Struct lookup by keyword key; null when absent.
    [[nodiscard]] const Value* Get(std::string_view keyword) const;
};

class JanetDataError : public std::runtime_error {
  public:
    JanetDataError(int line, int column, const std::string& message);
    [[nodiscard]] int Line() const {
        return line_;
    }

  private:
    int line_;
};

// Reads exactly one top-level value (comments aside); more than one, or
// none, is an error -- a definition file is one struct.
[[nodiscard]] Value ParseJanetData(std::string_view source);

} // namespace ned::editor::janetdata

#endif // NED_EDITOR_JANETDATA_H
