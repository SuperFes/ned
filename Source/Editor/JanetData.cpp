#include "JanetData.h"

namespace ned::editor::janetdata {

JanetDataError::JanetDataError(int line, int column, const std::string& message) : std::runtime_error(std::to_string(line) + ":" + std::to_string(column) + ": " + message), line_(line) {
}

const Value* Value::Get(std::string_view keyword) const {
    for (std::size_t i = 0; i + 1 < pairs.size(); i += 2) {
        if (pairs[i].kind == Kind::Keyword && pairs[i].text == keyword) {
            return &pairs[i + 1];
        }
    }
    return nullptr;
}

namespace {

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

    class Reader {
      public:
        explicit Reader(std::string_view source) : src_(source) {
        }

        Value ReadOne() {
            SkipSpaceAndComments();
            if (AtEnd()) {
                Fail("expected a value");
            }
            Value value = ReadValue();
            SkipSpaceAndComments();
            if (!AtEnd()) {
                Fail("more than one top-level value -- a definition file is one struct");
            }
            return value;
        }

      private:
        std::string_view src_;
        std::size_t      pos_  = 0;
        int              line_ = 1;
        int              col_  = 1;

        [[nodiscard]] bool AtEnd() const {
            return pos_ >= src_.size();
        }
        [[nodiscard]] char Peek() const {
            return pos_ < src_.size() ? src_[pos_] : '\0';
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
            throw JanetDataError(line_, col_, message);
        }

        void SkipSpaceAndComments() {
            for (;;) {
                while (!AtEnd() && IsSpace(Peek())) {
                    Take();
                }
                if (AtEnd() || Peek() != '#') {
                    return;
                }
                while (!AtEnd() && Peek() != '\n') {
                    Take();
                }
            }
        }

        static bool IsDelimiter(char c) {
            return IsSpace(c) || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '"' ||
                   c == '#' || c == '\0';
        }

        Value ReadValue() {
            const int  line = line_;
            const char c    = Peek();
            if (c == '{') {
                Take();
                Value value{.kind = Value::Kind::Struct, .line = line};
                for (;;) {
                    SkipSpaceAndComments();
                    if (AtEnd()) {
                        Fail("unclosed '{'");
                    }
                    if (Peek() == '}') {
                        Take();
                        return value;
                    }
                    value.pairs.push_back(ReadValue());
                    SkipSpaceAndComments();
                    if (AtEnd() || Peek() == '}') {
                        Fail("struct key with no value");
                    }
                    value.pairs.push_back(ReadValue());
                }
            }
            if (c == '[' || c == '(') {
                const char closer = c == '[' ? ']' : ')';
                Take();
                Value value{.kind = Value::Kind::Tuple, .line = line};
                for (;;) {
                    SkipSpaceAndComments();
                    if (AtEnd()) {
                        Fail(std::string("unclosed '") + c + "'");
                    }
                    if (Peek() == closer) {
                        Take();
                        return value;
                    }
                    if (Peek() == ']' || Peek() == ')') {
                        Fail("mismatched closer");
                    }
                    value.items.push_back(ReadValue());
                }
            }
            if (c == '"') {
                return Value{.kind = Value::Kind::String, .text = ReadString(), .line = line};
            }
            if (c == ':') {
                Take();
                return Value{.kind = Value::Kind::Keyword, .text = ReadSymbolText(/*allowEmpty=*/false), .line = line};
            }
            if (c == '}' || c == ']' || c == ')') {
                Fail("unexpected closer");
            }
            if (c == '\'' || c == '~' || c == ',' || c == ';' || c == '|' || c == '`' || c == '@') {
                Fail(std::string("'") + c + "' is not data syntax -- a definition file holds no code");
            }
            const std::string symbol = ReadSymbolText(/*allowEmpty=*/false);
            if (symbol == "true") {
                return Value{.kind = Value::Kind::Bool, .boolean = true, .line = line};
            }
            if (symbol == "false") {
                return Value{.kind = Value::Kind::Bool, .boolean = false, .line = line};
            }
            if (symbol == "nil") {
                return Value{.kind = Value::Kind::Nil, .line = line};
            }
            return Value{.kind = Value::Kind::Symbol, .text = symbol, .line = line};
        }

        std::string ReadSymbolText(bool allowEmpty) {
            std::string text;
            while (!AtEnd() && !IsDelimiter(Peek()) && Peek() != ':') {
                text += Take();
            }
            if (text.empty() && !allowEmpty) {
                Fail("expected a name");
            }
            return text;
        }

        // Janet's own string escapes -- the same set QueryData.cpp's Janet
        // dialect reads.
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
                        const int hi = AtEnd() ? -1 : HexValue(Take());
                        const int lo = AtEnd() ? -1 : HexValue(Take());
                        if (hi < 0 || lo < 0) {
                            Fail("bad \\x escape");
                        }
                        out += static_cast<char>(hi * 16 + lo);
                        break;
                    }
                    default:
                        Fail(std::string("unknown string escape '\\") + e + "'");
                }
            }
        }
    };

} // namespace

Value ParseJanetData(std::string_view source) {
    return Reader(source).ReadOne();
}

} // namespace ned::editor::janetdata
