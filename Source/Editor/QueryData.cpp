#include "QueryData.h"

#include <cstdio>
#include <utility>

namespace ned::editor::querydata {

QueryDataError::QueryDataError(int line, int column, const std::string& message) : std::runtime_error(std::to_string(line) + ":" + std::to_string(column) + ": " + message), line_(line), column_(column) {
}

namespace {

    enum class Dialect { Janet,
                         Scm };

    bool IsSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    int HexValue(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    // One reader for both spellings: the dialect decides the comment marker,
    // the predicate sigil, the string escape rules, and which bytes are
    // outright errors.
    class Reader {
      public:
        Reader(std::string_view source, Dialect dialect) : src_(source), dialect_(dialect) {
        }

        std::vector<Form> ReadAll() {
            std::vector<Form> forms;
            for (;;) {
                SkipSpace();
                if (AtEnd()) {
                    return forms;
                }
                if (Peek() == ')' || Peek() == ']') {
                    Fail("unexpected closer");
                }
                ReadInto(forms, /*inList=*/false);
            }
        }

      private:
        std::string_view src_;
        Dialect          dialect_;
        std::size_t      pos_  = 0;
        int              line_ = 1;
        int              col_  = 1;

        [[nodiscard]] bool AtEnd() const {
            return pos_ >= src_.size();
        }
        [[nodiscard]] char Peek(std::size_t ahead = 0) const {
            return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
        }
        char Take() {
            const char c = src_[pos_++];
            if (c == '\n') {
                ++line_;
                col_ = 1;
            }
            else {
                ++col_;
            }
            return c;
        }
        [[noreturn]] void Fail(const std::string& message) const {
            throw QueryDataError(line_, col_, message);
        }
        [[nodiscard]] char CommentMarker() const {
            return dialect_ == Dialect::Janet ? '#' : ';';
        }
        [[nodiscard]] char PredicateSigil() const {
            return dialect_ == Dialect::Janet ? ':' : '#';
        }
        [[nodiscard]] bool IsSymbolBreak(char c) const {
            return IsSpace(c) || c == '(' || c == ')' || c == '[' || c == ']' || c == '"' || c == CommentMarker() || c == '\0';
        }

        void SkipSpace() {
            while (!AtEnd() && IsSpace(Peek())) {
                Take();
            }
        }

        // Reads one item (or a comment) and appends it, attaching a
        // quantifier to the item before it.
        void ReadInto(std::vector<Form>& out, bool inList) {
            const int  line = line_;
            const char c    = Peek();
            if (c == CommentMarker()) {
                Take();
                std::string text;
                while (!AtEnd() && Peek() != '\n') {
                    text += Take();
                }
                out.push_back(Form{.kind = Form::Kind::Comment, .text = std::move(text), .line = line});
                return;
            }
            if (c == '(') {
                Take();
                SkipSpace();
                if (Peek() == PredicateSigil()) {
                    if (!inList) {
                        Fail("a predicate must sit inside a pattern");
                    }
                    Take();
                    Form predicate{.kind = Form::Kind::Predicate, .text = ReadSymbolText(), .line = line};
                    ReadItems(predicate.items, ')');
                    out.push_back(std::move(predicate));
                    return;
                }
                Form list{.kind = Form::Kind::List, .line = line};
                ReadItems(list.items, ')');
                out.push_back(std::move(list));
                return;
            }
            if (c == '[') {
                Take();
                Form alternation{.kind = Form::Kind::Alternation, .line = line};
                ReadItems(alternation.items, ']');
                out.push_back(std::move(alternation));
                return;
            }
            if (c == '"') {
                out.push_back(Form{.kind = Form::Kind::String, .text = ReadString(), .line = line});
                return;
            }
            if (dialect_ == Dialect::Janet && (c == ';' || c == '\'' || c == '~' || c == ',' || c == '|' || c == '`')) {
                Fail(std::string("'") + c + "' is not query syntax (a tree-sitter file not converted? comments are '#', predicates ':eq?')");
            }
            if (dialect_ == Dialect::Scm && c == '#') {
                Fail("'#' outside a predicate head");
            }
            std::string symbol = ReadSymbolText();
            if (symbol == "*" || symbol == "+" || symbol == "?") {
                if (out.empty() || !CanQuantify(out.back())) {
                    Fail("quantifier with nothing to quantify");
                }
                out.back().quantifier = symbol[0];
                return;
            }
            if (symbol.size() == 2 && symbol[0] == '_' && (symbol[1] == '*' || symbol[1] == '+' || symbol[1] == '?')) {
                out.push_back(Form{.kind = Form::Kind::Symbol, .text = "_", .quantifier = symbol[1], .line = line});
                return;
            }
            if (dialect_ == Dialect::Janet && symbol.front() == ':') {
                Fail("keyword outside a predicate head");
            }
            out.push_back(Form{.kind = Form::Kind::Symbol, .text = std::move(symbol), .line = line});
        }

        static bool CanQuantify(const Form& form) {
            switch (form.kind) {
                case Form::Kind::List:
                case Form::Kind::Alternation:
                case Form::Kind::String:
                    return form.quantifier == 0;
                case Form::Kind::Symbol:
                    return form.text == "_" && form.quantifier == 0;
                default:
                    return false;
            }
        }

        void ReadItems(std::vector<Form>& items, char closer) {
            for (;;) {
                SkipSpace();
                if (AtEnd()) {
                    Fail(std::string("unclosed '") + (closer == ')' ? '(' : '[') + "'");
                }
                if (Peek() == closer) {
                    Take();
                    return;
                }
                if (Peek() == ')' || Peek() == ']') {
                    Fail("mismatched closer");
                }
                ReadInto(items, /*inList=*/true);
            }
        }

        std::string ReadSymbolText() {
            std::string text;
            while (!AtEnd() && !IsSymbolBreak(Peek())) {
                text += Take();
            }
            if (text.empty()) {
                Fail("expected a symbol");
            }
            return text;
        }

        std::string ReadString() {
            Take(); // opening quote
            std::string out;
            for (;;) {
                if (AtEnd()) {
                    Fail("unterminated string");
                }
                const char c = Take();
                if (c == '"') {
                    return out;
                }
                if (c == '\n' && dialect_ == Dialect::Scm) {
                    Fail("newline inside a string");
                }
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (AtEnd()) {
                    Fail("unterminated escape");
                }
                const char e = Take();
                switch (e) {
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case '0':
                        out += '\0';
                        break;
                    default:
                        if (dialect_ == Dialect::Scm) {
                            out += e; // tree-sitter: any other escaped byte is that byte
                            break;
                        }
                        switch (e) {
                            case '\\':
                                out += '\\';
                                break;
                            case '"':
                                out += '"';
                                break;
                            case 'e':
                                out += '\x1b';
                                break;
                            case 'f':
                                out += '\f';
                                break;
                            case 'v':
                                out += '\v';
                                break;
                            case 'a':
                                out += '\a';
                                break;
                            case 'b':
                                out += '\b';
                                break;
                            case 'z':
                                out += '\0';
                                break;
                            case 'x': {
                                const int hi = HexValue(Peek()), lo = HexValue(Peek(1));
                                if (hi < 0 || lo < 0) {
                                    Fail("bad \\x escape");
                                }
                                Take();
                                Take();
                                out += static_cast<char>(hi * 16 + lo);
                                break;
                            }
                            default:
                                Fail(std::string("unknown string escape '\\") + e + "'");
                        }
                }
            }
        }
    };

    // tree-sitter's own escapes on the way out: it reads exactly these four
    // plus "any other byte is itself", so this is the inverse of ReadString
    // in Scm dialect.
    void AppendQueryString(std::string& out, std::string_view text) {
        out += '"';
        for (const char c : text) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                default:
                    out += c;
            }
        }
        out += '"';
    }

    void AppendJanetString(std::string& out, std::string_view text) {
        out += '"';
        for (const char c : text) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof buffer, "\\x%02x", static_cast<unsigned char>(c));
                        out += buffer;
                    }
                    else {
                        out += c;
                    }
            }
        }
        out += '"';
    }

    class QueryWriter {
      public:
        explicit QueryWriter(bool preserveLines) : preserveLines_(preserveLines) {
        }

        std::string Write(const std::vector<Form>& forms) {
            for (const Form& form : forms) {
                if (form.kind == Form::Kind::Comment) {
                    continue;
                }
                Place(form.line, /*topLevel=*/true,
                      /*startsPattern=*/form.kind == Form::Kind::List || form.kind == Form::Kind::Alternation);
                WriteForm(form);
            }
            out_ += '\n';
            return std::move(out_);
        }

      private:
        bool        preserveLines_;
        std::string out_;
        int         line_       = 1;
        bool        lineIsEmpty = true;

        void Place(int line, bool topLevel, bool startsPattern = false) {
            if (preserveLines_ && line > line_) {
                out_.append(static_cast<std::size_t>(line - line_), '\n');
                line_       = line;
                lineIsEmpty = true;
            }
            else if (!preserveLines_ && topLevel && startsPattern && !lineIsEmpty) {
                out_ += '\n'; // one pattern per line; a trailing capture stays with its pattern
                lineIsEmpty = true;
            }
            if (!lineIsEmpty) {
                out_ += ' ';
            }
            lineIsEmpty = false;
        }

        void WriteForm(const Form& form) {
            switch (form.kind) {
                case Form::Kind::Comment:
                    return;
                case Form::Kind::Symbol:
                    out_ += form.text;
                    break;
                case Form::Kind::String:
                    AppendQueryString(out_, form.text);
                    break;
                case Form::Kind::Predicate:
                    out_ += "(#";
                    out_ += form.text;
                    WriteItems(form.items);
                    out_ += ')';
                    return;
                case Form::Kind::List:
                    out_ += '(';
                    lineIsEmpty = true;
                    WriteItems(form.items);
                    out_ += ')';
                    break;
                case Form::Kind::Alternation:
                    out_ += '[';
                    lineIsEmpty = true;
                    WriteItems(form.items);
                    out_ += ']';
                    break;
            }
            if (form.quantifier != 0) {
                out_ += form.quantifier;
            }
        }

        void WriteItems(const std::vector<Form>& items) {
            for (const Form& item : items) {
                if (item.kind == Form::Kind::Comment) {
                    continue;
                }
                Place(item.line, /*topLevel=*/false);
                WriteForm(item);
            }
            lineIsEmpty = false;
        }
    };

} // namespace

std::vector<Form> ParseJanet(std::string_view source) {
    return Reader(source, Dialect::Janet).ReadAll();
}

std::vector<Form> ParseScm(std::string_view source) {
    return Reader(source, Dialect::Scm).ReadAll();
}

std::string ToQueryText(const std::vector<Form>& forms, bool preserveLines) {
    return QueryWriter(preserveLines).Write(forms);
}

std::string ConvertScmToJanet(std::string_view scm) {
    std::string out;
    out.reserve(scm.size());
    int  line = 1;
    int  col  = 1;
    auto fail = [&](const std::string& message) { throw QueryDataError(line, col, message); };
    for (std::size_t i = 0; i < scm.size(); ++i) {
        const char c = scm[i];
        if (c == '\n') {
            ++line;
            col = 0;
        }
        ++col;
        if (c == ';') {
            out += '#';
            while (i + 1 < scm.size() && scm[i + 1] != '\n') {
                out += scm[++i]; // the comment body is verbatim, whatever it contains
            }
            continue;
        }
        if (c == '(' && i + 1 < scm.size() && scm[i + 1] == '#') {
            out += "(:";
            ++i;
            continue;
        }
        if (c == '"') {
            // Decode by tree-sitter's rules, re-encode by Janet's.
            std::string decoded;
            std::size_t j = i + 1;
            for (;; ++j) {
                if (j >= scm.size() || scm[j] == '\n') {
                    fail("unterminated string");
                }
                if (scm[j] == '"') {
                    break;
                }
                if (scm[j] != '\\') {
                    decoded += scm[j];
                    continue;
                }
                if (++j >= scm.size()) {
                    fail("unterminated escape");
                }
                switch (scm[j]) {
                    case 'n':
                        decoded += '\n';
                        break;
                    case 'r':
                        decoded += '\r';
                        break;
                    case 't':
                        decoded += '\t';
                        break;
                    case '0':
                        decoded += '\0';
                        break;
                    default:
                        decoded += scm[j];
                }
            }
            AppendJanetString(out, decoded);
            col += static_cast<int>(j - i);
            i = j;
            continue;
        }
        if (c == '#' || c == '\'' || c == '~' || c == ',' || c == '|' || c == '`') {
            fail(std::string("'") + c + "' has no Janet spelling here");
        }
        out += c;
    }
    return out;
}

int LineOfOffset(std::string_view text, std::size_t offset) {
    int line = 1;
    for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

} // namespace ned::editor::querydata
