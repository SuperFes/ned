#include "Regex.h"

#include <string>

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/Unicode.h"

namespace ned::editor::grammar::compile {

namespace {

    class RegexParser {
      public:
        RegexParser(std::string_view pattern, bool caseInsensitive) : src_(pattern), caseInsensitive_(caseInsensitive) {
        }

        RegexNode ParseAll() {
            RegexNode node = ParseAlternation();
            if (!AtEnd())
                Fail(std::string("unexpected '") + Peek() + "'");
            return node;
        }

      private:
        std::string_view src_;
        std::size_t      pos_ = 0;
        bool             caseInsensitive_;

        [[nodiscard]] bool AtEnd() const {
            return pos_ >= src_.size();
        }
        [[nodiscard]] char Peek(std::size_t ahead = 0) const {
            return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
        }
        [[noreturn]] void Fail(const std::string& message) const {
            throw CompileError("regex /" + std::string(src_) + "/ at offset " + std::to_string(pos_) + ": " + message);
        }
        bool Consume(char c) {
            if (!AtEnd() && src_[pos_] == c) {
                ++pos_;
                return true;
            }
            return false;
        }
        bool ConsumePrefix(std::string_view prefix) {
            if (StartsWith(prefix)) {
                pos_ += prefix.size();
                return true;
            }
            return false;
        }
        [[nodiscard]] bool StartsWith(std::string_view prefix) const {
            return src_.substr(pos_, prefix.size()) == prefix;
        }

        // One UTF-8 codepoint of the pattern text.
        std::uint32_t TakeCodepoint() {
            const auto lead = static_cast<unsigned char>(src_[pos_++]);
            if (lead < 0x80)
                return lead;
            std::uint32_t codepoint = 0;
            std::size_t   extra     = 0;
            if ((lead & 0xE0) == 0xC0) {
                codepoint = lead & 0x1F;
                extra     = 1;
            }
            else if ((lead & 0xF0) == 0xE0) {
                codepoint = lead & 0x0F;
                extra     = 2;
            }
            else if ((lead & 0xF8) == 0xF0) {
                codepoint = lead & 0x07;
                extra     = 3;
            }
            else {
                Fail("invalid UTF-8 in pattern");
            }
            for (std::size_t i = 0; i < extra; ++i) {
                if (AtEnd())
                    Fail("truncated UTF-8 in pattern");
                codepoint = (codepoint << 6) | (static_cast<unsigned char>(src_[pos_++]) & 0x3F);
            }
            return codepoint;
        }

        static RegexNode Class(CharacterSet set) {
            RegexNode node;
            node.kind = RegexNode::Kind::Class;
            node.set  = std::move(set);
            return node;
        }

        RegexNode Fold(CharacterSet set) const {
            return Class(caseInsensitive_ ? unicode::CaseFold(set) : std::move(set));
        }

        RegexNode ParseAlternation() {
            std::vector<RegexNode> branches;
            branches.push_back(ParseConcat());
            while (Consume('|'))
                branches.push_back(ParseConcat());
            if (branches.size() == 1)
                return std::move(branches.front());
            RegexNode node;
            node.kind     = RegexNode::Kind::Alternation;
            node.children = std::move(branches);
            return node;
        }

        RegexNode ParseConcat() {
            std::vector<RegexNode> items;
            while (!AtEnd() && Peek() != '|' && Peek() != ')')
                items.push_back(ParseRepetition());
            if (items.empty())
                return RegexNode{};
            if (items.size() == 1)
                return std::move(items.front());
            RegexNode node;
            node.kind     = RegexNode::Kind::Concat;
            node.children = std::move(items);
            return node;
        }

        std::uint32_t ParseDecimal() {
            if (AtEnd() || Peek() < '0' || Peek() > '9')
                Fail("expected a number in a counted repetition");
            std::uint32_t value = 0;
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
                value = value * 10 + static_cast<std::uint32_t>(src_[pos_++] - '0');
            return value;
        }

        RegexNode ParseRepetition() {
            RegexNode atom = ParseAtom();
            for (;;) {
                std::uint32_t min = 0, max = kRegexUnbounded;
                if (Consume('*')) {
                    min = 0;
                }
                else if (Consume('+')) {
                    min = 1;
                }
                else if (Consume('?')) {
                    min = 0;
                    max = 1;
                }
                else if (Peek() == '{' && Peek(1) >= '0' && Peek(1) <= '9') {
                    ++pos_;
                    min = ParseDecimal();
                    max = min;
                    if (Consume(',')) {
                        max = (Peek() >= '0' && Peek() <= '9') ? ParseDecimal() : kRegexUnbounded;
                    }
                    if (!Consume('}'))
                        Fail("unclosed counted repetition");
                    if (max != kRegexUnbounded && max < min)
                        Fail("counted repetition with max below min");
                }
                else {
                    return atom;
                }
                Consume('?'); // lazy marker: irrelevant to a DFA
                RegexNode node;
                node.kind = RegexNode::Kind::Repetition;
                node.min  = min;
                node.max  = max;
                node.children.push_back(std::move(atom));
                atom = std::move(node);
            }
        }

        RegexNode ParseAtom() {
            const char c = Peek();
            if (c == '(') {
                ++pos_;
                if (Consume('?')) {
                    if (Consume(':')) {
                        // non-capturing
                    }
                    else {
                        // inline flags: only case-insensitivity is meaningful here
                        bool negate = false, sawFlag = false;
                        while (!AtEnd() && Peek() != ':' && Peek() != ')') {
                            const char f = src_[pos_++];
                            if (f == '-')
                                negate = true;
                            else if (f == 'i') {
                                caseInsensitive_ = !negate;
                                sawFlag          = true;
                            }
                            else if (f == 'u' || f == 'x' || f == 's' || f == 'm' || f == 'U') {
                                Fail(std::string("unsupported inline flag '") + f + "'");
                            }
                            else
                                Fail(std::string("unknown inline flag '") + f + "'");
                        }
                        if (Consume(')')) {
                            (void)sawFlag;
                            return RegexNode{}; // flags apply to the rest of the enclosing group
                        }
                        if (!Consume(':'))
                            Fail("malformed group flags");
                    }
                }
                RegexNode inner = ParseAlternation();
                if (!Consume(')'))
                    Fail("unclosed group");
                return inner;
            }
            if (c == ')')
                Fail("unmatched ')'");
            if (c == '[') {
                ++pos_;
                return Fold(ParseClass());
            }
            if (c == '.') {
                ++pos_;
                return Class(CharacterSet::FromChar('\n').Negate());
            }
            if (c == '^' || c == '$')
                Fail("assertions are not supported");
            if (c == '*' || c == '+' || c == '?')
                Fail(std::string("nothing to repeat before '") + c + "'");
            if (c == '{' && Peek(1) >= '0' && Peek(1) <= '9')
                Fail("nothing to repeat before '{'");
            if (c == '\\') {
                ++pos_;
                return Fold(ParseEscape(/*inClass=*/false));
            }
            return Fold(CharacterSet::FromChar(TakeCodepoint()));
        }

        static std::uint32_t HexValue(char c) {
            if (c >= '0' && c <= '9')
                return static_cast<std::uint32_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<std::uint32_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<std::uint32_t>(c - 'A' + 10);
            return static_cast<std::uint32_t>(-1);
        }

        std::uint32_t ParseHex(std::size_t fixedDigits) {
            std::uint32_t value = 0;
            if (fixedDigits == 0) {
                if (!Consume('{'))
                    Fail("expected '{' after a hex escape");
                std::size_t digits = 0;
                while (!AtEnd() && Peek() != '}') {
                    const std::uint32_t h = HexValue(src_[pos_]);
                    if (h == static_cast<std::uint32_t>(-1))
                        Fail("bad hex digit in escape");
                    value = value * 16 + h;
                    ++pos_;
                    ++digits;
                }
                if (!Consume('}') || digits == 0)
                    Fail("unclosed hex escape");
            }
            else {
                for (std::size_t i = 0; i < fixedDigits; ++i) {
                    if (AtEnd())
                        Fail("truncated hex escape");
                    const std::uint32_t h = HexValue(src_[pos_]);
                    if (h == static_cast<std::uint32_t>(-1))
                        Fail("bad hex digit in escape");
                    value = value * 16 + h;
                    ++pos_;
                }
            }
            if (value >= kCodepointEnd || (value >= 0xD800 && value <= 0xDFFF))
                Fail("escape names an invalid codepoint");
            return value;
        }

        CharacterSet ParseProperty(bool negated) {
            std::string name;
            if (Consume('{')) {
                while (!AtEnd() && Peek() != '}')
                    name += src_[pos_++];
                if (!Consume('}'))
                    Fail("unclosed \\p{...}");
            }
            else {
                if (AtEnd())
                    Fail("truncated \\p escape");
                name = std::string(1, src_[pos_++]);
            }
            const std::optional<CharacterSet> set = unicode::Property(name);
            if (!set)
                Fail("unknown Unicode property \\p{" + name + "}");
            return negated ? set->Negate() : *set;
        }

        // After the backslash.
        CharacterSet ParseEscape(bool inClass) {
            if (AtEnd())
                Fail("trailing backslash");
            const char e = src_[pos_++];
            switch (e) {
                case 'n':
                    return CharacterSet::FromChar('\n');
                case 'r':
                    return CharacterSet::FromChar('\r');
                case 't':
                    return CharacterSet::FromChar('\t');
                case 'f':
                    return CharacterSet::FromChar('\f');
                case 'v':
                    return CharacterSet::FromChar('\v');
                case 'a':
                    return CharacterSet::FromChar('\a');
                case '0':
                    return CharacterSet::FromChar(0);
                case 'x':
                    return CharacterSet::FromChar(Peek() == '{' ? ParseHex(0) : ParseHex(2));
                case 'u':
                    return CharacterSet::FromChar(Peek() == '{' ? ParseHex(0) : ParseHex(4));
                case 'U':
                    return CharacterSet::FromChar(Peek() == '{' ? ParseHex(0) : ParseHex(8));
                case 'p':
                    return ParseProperty(false);
                case 'P':
                    return ParseProperty(true);
                case 'd':
                    return *unicode::Property("Nd");
                case 'D':
                    return unicode::Property("Nd")->Negate();
                case 's':
                    return WhiteSpace();
                case 'S':
                    return WhiteSpace().Negate();
                case 'w':
                    return WordChars();
                case 'W':
                    return WordChars().Negate();
                case 'b':
                case 'B':
                case 'A':
                case 'z':
                    Fail("assertions are not supported");
                default:
                    break;
            }
            if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z') || (e >= '1' && e <= '9'))
                Fail(std::string("unknown escape '\\") + e + "'");
            (void)inClass;
            --pos_;
            return CharacterSet::FromChar(TakeCodepoint());
        }

        // Unicode White_Space, the meaning of an unsubstituted `\s`.
        static CharacterSet WhiteSpace() {
            return CharacterSet::FromRange(0x09, 0x0D)
                .AddChar(0x20)
                .AddChar(0x85)
                .AddChar(0xA0)
                .AddChar(0x1680)
                .AddRange(0x2000, 0x200A)
                .AddChar(0x2028)
                .AddChar(0x2029)
                .AddChar(0x202F)
                .AddChar(0x205F)
                .AddChar(0x3000);
        }

        // Unicode `\w`: alphabetic, marks, decimal numbers, connector
        // punctuation and the join controls.
        static CharacterSet WordChars() {
            CharacterSet set = *unicode::Property("L");
            set              = set.Add(*unicode::Property("M")).Add(*unicode::Property("Nd")).Add(*unicode::Property("Pc"));
            set              = set.AddChar(0x200C).AddChar(0x200D);
            return set;
        }

        // After the opening bracket. The reference's class syntax: a union
        // of items, then `&&` (intersection), `--` (difference) and `~~`
        // (symmetric difference) between unions, left to right, all at
        // one precedence below union: [a-z&&[^aeiou]].
        CharacterSet ParseClass() {
            const bool   negated = Consume('^');
            CharacterSet set     = ParseClassUnion(/*first=*/true);
            for (;;) {
                if (AtEnd())
                    Fail("unclosed character class");
                if (Consume(']'))
                    break;
                if (ConsumePrefix("&&")) {
                    const CharacterSet rhs = ParseClassUnion(false);
                    set                    = set.Difference(set.Difference(rhs));
                }
                else if (ConsumePrefix("--")) {
                    set = set.Difference(ParseClassUnion(false));
                }
                else if (ConsumePrefix("~~")) {
                    const CharacterSet rhs = ParseClassUnion(false);
                    set                    = set.Add(rhs).Difference(set.Difference(set.Difference(rhs)));
                }
                else {
                    Fail("malformed character class");
                }
            }
            return negated ? set.Negate() : set;
        }

        // Items up to the closing bracket or a set operator; `first` allows
        // a literal `]` right after the opening bracket.
        CharacterSet ParseClassUnion(bool first) {
            CharacterSet set;
            for (;;) {
                if (AtEnd())
                    Fail("unclosed character class");
                if (Peek() == ']' && !first)
                    break;
                // A set operator needs a left operand: at the head of a class
                // `--` is a literal dash (powershell's `[--][gG][tT]`).
                if (!first && (StartsWith("&&") || StartsWith("--") || StartsWith("~~")))
                    break;
                first = false;
                if (Peek() == '[') {
                    if (ConsumePrefix("[:")) {
                        std::string name;
                        while (!AtEnd() && Peek() != ':')
                            name += src_[pos_++];
                        if (!ConsumePrefix(":]"))
                            Fail("unclosed POSIX class");
                        set = set.Add(PosixClass(name));
                        continue;
                    }
                    ++pos_;
                    set = set.Add(ParseClass());
                    continue;
                }
                CharacterSet item;
                bool         single = true;
                if (Consume('\\')) {
                    item   = ParseEscape(/*inClass=*/true);
                    single = item.RangeCount() == 1 && item.Ranges()[0].end == item.Ranges()[0].start + 1;
                }
                else {
                    item = CharacterSet::FromChar(TakeCodepoint());
                }
                if (single && Peek() == '-' && Peek(1) != ']' && Peek(1) != '\0' && Peek(1) != '-') {
                    ++pos_;
                    const std::uint32_t start = item.Ranges()[0].start;
                    std::uint32_t       end;
                    if (Consume('\\')) {
                        const CharacterSet endSet = ParseEscape(true);
                        if (endSet.RangeCount() != 1 || endSet.Ranges()[0].end != endSet.Ranges()[0].start + 1)
                            Fail("a class range must end in a single character");
                        end = endSet.Ranges()[0].start;
                    }
                    else {
                        end = TakeCodepoint();
                    }
                    if (end < start)
                        Fail("class range out of order");
                    item = CharacterSet::FromRange(start, end);
                }
                set = set.Add(item);
            }
            return set;
        }

        CharacterSet PosixClass(const std::string& name) {
            if (name == "alpha")
                return CharacterSet::FromRange('a', 'z').AddRange('A', 'Z');
            if (name == "digit")
                return CharacterSet::FromRange('0', '9');
            if (name == "alnum")
                return CharacterSet::FromRange('a', 'z').AddRange('A', 'Z').AddRange('0', '9');
            if (name == "space")
                return CharacterSet::FromRange('\t', '\r').AddChar(' ');
            if (name == "upper")
                return CharacterSet::FromRange('A', 'Z');
            if (name == "lower")
                return CharacterSet::FromRange('a', 'z');
            if (name == "xdigit")
                return CharacterSet::FromRange('0', '9').AddRange('a', 'f').AddRange('A', 'F');
            if (name == "punct")
                return CharacterSet::FromRange('!', '/').AddRange(':', '@').AddRange('[', '`').AddRange('{', '~');
            if (name == "word")
                return CharacterSet::FromRange('a', 'z').AddRange('A', 'Z').AddRange('0', '9').AddChar('_');
            if (name == "blank")
                return CharacterSet::FromChar(' ').AddChar('\t');
            if (name == "cntrl")
                return CharacterSet::FromRange(0, 0x1F).AddChar(0x7F);
            if (name == "print")
                return CharacterSet::FromRange(0x20, 0x7E);
            if (name == "graph")
                return CharacterSet::FromRange(0x21, 0x7E);
            if (name == "ascii")
                return CharacterSet::FromRange(0, 0x7F);
            Fail("unknown POSIX class [:" + name + ":]");
        }
    };

} // namespace

RegexNode ParseRegex(std::string_view pattern, bool caseInsensitive) {
    return RegexParser(pattern, caseInsensitive).ParseAll();
}

} // namespace ned::editor::grammar::compile
