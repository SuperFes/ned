#include "Editor/Snippet.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <random>

#include "Editor/RegexPattern.h"

namespace ned::editor {

namespace {

    // One tokenized tabstop marker, before substitution.
    struct RawStop {
        int                             index;
        std::string                     placeholder; // nested markers already stripped
        bool                            hasPlaceholder;
        std::optional<SnippetTransform> transform; // set only for `${N/regex/format/flags}`
    };

    std::optional<RawStop>                  ParseTabstopAt(std::string_view body, std::size_t& pos);
    std::optional<SnippetTransform>         ParseTransformSuffix(std::string_view body, std::size_t& pos);
    std::optional<std::vector<std::string>> ParseChoiceSuffix(std::string_view body, std::size_t& pos);
    std::string                             ApplyTransform(std::string_view fieldText, const SnippetTransform& transform);
    std::string                             ResolveVariable(std::string_view name, std::string_view defaultText, bool hasDefault,
                                                            const SnippetVariables& variables);

    // Reads placeholder content from just past the ':' up to the matching
    // unescaped '}', consuming it. A nested tabstop marker contributes its
    // own placeholder text only (the inner stop is dropped -- see the
    // header's cuts). nullopt when the closing '}' is missing.
    std::optional<std::string> ParsePlaceholderContent(std::string_view body, std::size_t& pos) {
        std::string content;
        while (pos < body.size()) {
            const char c = body[pos];
            if (c == '\\' && pos + 1 < body.size()) {
                const char next = body[pos + 1];
                if (next == '}' || next == '$' || next == '\\') {
                    content.push_back(next);
                    pos += 2;
                    continue;
                }
                content.push_back(c);
                ++pos;
                continue;
            }
            if (c == '}') {
                ++pos;
                return content;
            }
            if (c == '$') {
                std::size_t probe = pos;
                if (const auto inner = ParseTabstopAt(body, probe)) {
                    content += inner->placeholder;
                    pos = probe;
                    continue;
                }
            }
            content.push_back(c);
            ++pos;
        }
        return std::nullopt;
    }

    // Bounded so a pathological "$9999999999" can't overflow; anything past
    // the cap is treated as ill-formed and falls through as literal text.
    std::optional<int> ParseIndexDigits(std::string_view body, std::size_t& pos) {
        constexpr std::size_t kMaxIndexDigits = 6;
        const std::size_t     start           = pos;
        while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
            ++pos;
        }
        if (pos == start || pos - start > kMaxIndexDigits) {
            pos = start;
            return std::nullopt;
        }
        int value = 0;
        for (std::size_t i = start; i < pos; ++i) {
            value = value * 10 + (body[i] - '0');
        }
        return value;
    }

    // Attempts to parse a tabstop marker at body[pos] (which is '$'). On
    // success advances pos past the marker; on failure leaves pos untouched
    // so the caller emits the '$' literally.
    std::optional<RawStop> ParseTabstopAt(std::string_view body, std::size_t& pos) {
        std::size_t p = pos + 1;
        if (p >= body.size()) {
            return std::nullopt;
        }
        if (std::isdigit(static_cast<unsigned char>(body[p]))) {
            const auto index = ParseIndexDigits(body, p);
            if (!index) {
                return std::nullopt;
            }
            pos = p;
            return RawStop{*index, "", false};
        }
        if (body[p] != '{') {
            return std::nullopt;
        }
        ++p;
        const auto index = ParseIndexDigits(body, p);
        if (!index || p >= body.size()) {
            return std::nullopt;
        }
        if (body[p] == '}') {
            pos = p + 1;
            return RawStop{*index, "", false};
        }
        if (body[p] == '/') {
            auto transform = ParseTransformSuffix(body, p);
            if (!transform) {
                return std::nullopt;
            }
            pos = p;
            RawStop stop{*index, "", false};
            stop.transform = std::move(transform);
            return stop;
        }
        if (body[p] == '|') {
            auto choices = ParseChoiceSuffix(body, p);
            if (!choices || choices->empty()) {
                return std::nullopt;
            }
            pos = p;
            return RawStop{*index, std::move(choices->front()), true};
        }
        if (body[p] != ':') {
            return std::nullopt;
        }
        ++p;
        auto content = ParsePlaceholderContent(body, p);
        if (!content) {
            return std::nullopt;
        }
        pos = p;
        return RawStop{*index, std::move(*content), true};
    }

    // Reads /regex/format/flags} starting at body[pos] == '/' (the
    // ${N/regex/format/flags} transform production), consuming through the
    // closing '}'. '/' is a segment terminator inside regex/format -- '\/'
    // unescapes to a literal '/', any other backslash sequence (e.g. '\d',
    // '\.') passes through untouched for PCRE2 to interpret itself. The
    // format segment additionally tracks brace depth: a case-modifier
    // format reference like `${1:/upcase}` carries its own bare '/', which
    // must not terminate the segment the way a top-level one does. nullopt
    // (pos untouched by the caller's own contract) on anything unterminated.
    std::optional<SnippetTransform> ParseTransformSuffix(std::string_view body, std::size_t& pos) {
        std::size_t p           = pos + 1; // skip the leading '/'
        const auto  readSegment = [&](bool trackBraceDepth) -> std::optional<std::string> {
            std::string out;
            int         braceDepth = 0;
            while (p < body.size()) {
                const char c = body[p];
                if (c == '\\' && p + 1 < body.size()) {
                    if (body[p + 1] == '/') {
                        out.push_back('/');
                        p += 2;
                        continue;
                    }
                    out.push_back(c);
                    out.push_back(body[p + 1]);
                    p += 2;
                    continue;
                }
                if (trackBraceDepth && c == '{') {
                    ++braceDepth;
                    out.push_back(c);
                    ++p;
                    continue;
                }
                if (trackBraceDepth && c == '}' && braceDepth > 0) {
                    --braceDepth;
                    out.push_back(c);
                    ++p;
                    continue;
                }
                if (c == '/' && braceDepth == 0) {
                    ++p;
                    return out;
                }
                out.push_back(c);
                ++p;
            }
            return std::nullopt;
        };
        const auto regex = readSegment(false);
        if (!regex) {
            return std::nullopt;
        }
        const auto format = readSegment(true);
        if (!format) {
            return std::nullopt;
        }
        std::string flags;
        while (p < body.size() && body[p] != '}') {
            flags.push_back(body[p]);
            ++p;
        }
        if (p >= body.size()) {
            return std::nullopt;
        }
        pos = p + 1; // skip '}'
        SnippetTransform transform;
        transform.pattern = (flags.find('i') != std::string::npos ? "(?i)" : "") + *regex;
        transform.format  = *format;
        transform.global  = flags.find('g') != std::string::npos;
        return transform;
    }

    // Reads |choice1,choice2,...|} starting at body[pos] == '|' (the
    // ${N|a,b,c|} choice production), consuming through the closing '}'.
    // v1 has no picker UI (ROADMAP's own documented cut) -- the caller just
    // uses the first choice as the field's placeholder text, functionally a
    // ${N:first} placeholder from here on. nullopt on anything malformed
    // (a '|' not immediately followed by '}', or unterminated).
    std::optional<std::vector<std::string>> ParseChoiceSuffix(std::string_view body, std::size_t& pos) {
        std::size_t              p = pos + 1; // skip the leading '|'
        std::vector<std::string> choices;
        std::string              current;
        while (p < body.size()) {
            const char c = body[p];
            if (c == '\\' && p + 1 < body.size() && (body[p + 1] == ',' || body[p + 1] == '|' || body[p + 1] == '\\')) {
                current.push_back(body[p + 1]);
                p += 2;
                continue;
            }
            if (c == ',') {
                choices.push_back(std::move(current));
                current.clear();
                ++p;
                continue;
            }
            if (c == '|') {
                choices.push_back(std::move(current));
                ++p;
                if (p < body.size() && body[p] == '}') {
                    pos = p + 1;
                    return choices;
                }
                return std::nullopt;
            }
            current.push_back(c);
            ++p;
        }
        return std::nullopt;
    }

    bool IsIdentifierStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }
    bool IsIdentifierChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    std::string RandomDigits(int count) {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<int>  dist(0, 9);
        std::string                         out(static_cast<std::size_t>(count), '0');
        for (char& c : out) {
            c = static_cast<char>('0' + dist(rng));
        }
        return out;
    }

    std::string RandomHex(int count) {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        static constexpr char               kHexDigits[] = "0123456789abcdef";
        std::uniform_int_distribution<int>  dist(0, 15);
        std::string                         out(static_cast<std::size_t>(count), '0');
        for (char& c : out) {
            c = kHexDigits[dist(rng)];
        }
        return out;
    }

    std::string RandomUuidV4() {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<int>  byteDist(0, 255);
        unsigned char                       bytes[16];
        for (unsigned char& b : bytes) {
            b = static_cast<unsigned char>(byteDist(rng));
        }
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40); // version 4
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80); // variant 10xx
        char buf[37];
        std::snprintf(buf, sizeof(buf),
                      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0], bytes[1],
                      bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                      bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        return std::string(buf);
    }

    // The practical $TM_*/CLIPBOARD/CURRENT_* subset this codebase supports
    // -- see SnippetVariables' own doc comment for what's deliberately not
    // here (TM_CURRENT_WORD, BLOCK_COMMENT_*, LINE_COMMENT, WORKSPACE_*).
    // nullopt means "not a name this editor knows at all" (distinct from a
    // known variable resolving to ""), which is what lets ResolveVariable
    // tell "known but unset" apart from "genuinely unknown name".
    std::optional<std::string> LookupKnownVariable(std::string_view name, const SnippetVariables& variables) {
        if (name == "RANDOM") {
            return RandomDigits(6);
        }
        if (name == "RANDOM_HEX") {
            return RandomHex(6);
        }
        if (name == "UUID") {
            return RandomUuidV4();
        }
        using Member                                                      = std::string                                SnippetVariables::*;
        static const std::unordered_map<std::string_view, Member> kSimple = {
            {"TM_SELECTED_TEXT", &SnippetVariables::selectedText},
            {"TM_CURRENT_LINE", &SnippetVariables::currentLine},
            {"TM_LINE_NUMBER", &SnippetVariables::lineNumber},
            {"TM_LINE_INDEX", &SnippetVariables::lineIndex},
            {"TM_FILENAME", &SnippetVariables::filename},
            {"TM_FILENAME_BASE", &SnippetVariables::filenameBase},
            {"TM_DIRECTORY", &SnippetVariables::directory},
            {"TM_FILEPATH", &SnippetVariables::filepath},
            {"RELATIVE_FILEPATH", &SnippetVariables::relativeFilepath},
            {"CLIPBOARD", &SnippetVariables::clipboard},
            {"CURRENT_YEAR", &SnippetVariables::year},
            {"CURRENT_MONTH", &SnippetVariables::month},
            {"CURRENT_DATE", &SnippetVariables::date},
            {"CURRENT_HOUR", &SnippetVariables::hour},
            {"CURRENT_MINUTE", &SnippetVariables::minute},
            {"CURRENT_SECOND", &SnippetVariables::second},
        };
        const auto it = kSimple.find(name);
        if (it == kSimple.end()) {
            return std::nullopt;
        }
        return variables.*(it->second);
    }

    // A known-but-empty value (no selection for TM_SELECTED_TEXT, an empty
    // system clipboard, ...) is treated as "unset" the same as a genuinely
    // absent one -- both fall back to the variable's own default when the
    // body supplied one. A name this editor doesn't know at all falls back
    // to its default, else the bare name itself (VSCode's own convention --
    // reads as an intentional placeholder rather than vanishing silently).
    std::string ResolveVariable(std::string_view name, std::string_view defaultText, bool hasDefault,
                                const SnippetVariables& variables) {
        const std::optional<std::string> known = LookupKnownVariable(name, variables);
        if (known && !known->empty()) {
            return *known;
        }
        if (hasDefault) {
            return std::string(defaultText);
        }
        if (known) {
            return "";
        }
        return std::string(name);
    }

    // $NAME / ${NAME} / ${NAME:default} / ${NAME/regex/format/flags} -- the
    // LSP snippet grammar's `variable` production. Resolved immediately
    // against variables and folded into surrounding literal text rather
    // than becoming a SnippetField -- unlike a tabstop, a variable's value
    // never changes across a session's life, so there's nothing live to
    // track. On success advances pos past the whole construct and returns
    // the resolved text; nullopt (pos untouched) when body[pos] isn't a
    // variable reference at all -- ParseTabstopAt's own contract, since the
    // two are tried in sequence against the same leading '$'.
    std::optional<std::string> TryParseVariable(std::string_view body, std::size_t& pos,
                                                const SnippetVariables& variables) {
        std::size_t p = pos + 1;
        if (p >= body.size()) {
            return std::nullopt;
        }
        if (IsIdentifierStart(body[p])) {
            const std::size_t start = p;
            while (p < body.size() && IsIdentifierChar(body[p])) {
                ++p;
            }
            const std::string name(body.substr(start, p - start));
            pos = p;
            return ResolveVariable(name, "", false, variables);
        }
        if (body[p] != '{' || p + 1 >= body.size() || !IsIdentifierStart(body[p + 1])) {
            return std::nullopt;
        }
        ++p; // skip '{'
        const std::size_t nameStart = p;
        while (p < body.size() && IsIdentifierChar(body[p])) {
            ++p;
        }
        const std::string name(body.substr(nameStart, p - nameStart));
        if (p >= body.size()) {
            return std::nullopt;
        }
        if (body[p] == '}') {
            pos = p + 1;
            return ResolveVariable(name, "", false, variables);
        }
        if (body[p] == '/') {
            const auto transform = ParseTransformSuffix(body, p);
            if (!transform) {
                return std::nullopt;
            }
            pos = p;
            return ApplyTransform(ResolveVariable(name, "", false, variables), *transform);
        }
        if (body[p] != ':') {
            return std::nullopt;
        }
        ++p;
        // Default text: a plain literal scan to the matching unescaped '}',
        // deliberately not recursing into nested $-references the way a
        // tabstop placeholder's ParsePlaceholderContent does -- a variable
        // default naming another variable/tabstop is a rare enough
        // construct that a documented v1 cut is the right call here.
        std::string defaultText;
        while (p < body.size() && body[p] != '}') {
            if (body[p] == '\\' && p + 1 < body.size() && (body[p + 1] == '}' || body[p + 1] == '$' || body[p + 1] == '\\')) {
                defaultText.push_back(body[p + 1]);
                p += 2;
                continue;
            }
            defaultText.push_back(body[p]);
            ++p;
        }
        if (p >= body.size()) {
            return std::nullopt;
        }
        pos = p + 1;
        return ResolveVariable(name, defaultText, true, variables);
    }

    std::string ApplyCase(std::string_view text, std::string_view modifier) {
        std::string out(text);
        if (modifier == "upcase") {
            for (char& c : out) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        else if (modifier == "downcase") {
            for (char& c : out) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        else if (modifier == "capitalize") {
            bool first = true;
            for (char& c : out) {
                c     = static_cast<char>(first ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
                first = false;
            }
        }
        return out;
    }

    // Expands one transform's `format` string against a single regex match
    // -- the LSP spec's own `${N:/upcase}`/`${N:+if}`/`${N:-else}`/`${N:else}`
    // mini-language (VSCode's implementation, not TextMate's older
    // `\U...\E` convention -- see SnippetTransform's own doc comment).
    // Ill-formed format syntax degrades to a literal '$' rather than
    // erroring, ParseSnippet's own philosophy throughout this file.
    std::string ExpandTransformFormat(std::string_view format, std::string_view subject, const RegexMatch& match) {
        const auto groupParticipated = [&](int n) {
            return n >= 0 && static_cast<std::size_t>(n) < match.groups.size() && match.groups[static_cast<std::size_t>(n)].matched;
        };
        const auto groupValue = [&](int n) -> std::string {
            if (!groupParticipated(n)) {
                return "";
            }
            const RegexGroupSpan& g = match.groups[static_cast<std::size_t>(n)];
            return std::string(subject.substr(g.start, g.end - g.start));
        };

        std::string out;
        std::size_t pos = 0;
        while (pos < format.size()) {
            const char c = format[pos];
            if (c == '\\' && pos + 1 < format.size() && (format[pos + 1] == '$' || format[pos + 1] == '\\')) {
                out.push_back(format[pos + 1]);
                pos += 2;
                continue;
            }
            if (c != '$') {
                out.push_back(c);
                ++pos;
                continue;
            }
            std::size_t p = pos + 1;
            if (p < format.size() && std::isdigit(static_cast<unsigned char>(format[p]))) {
                const std::size_t start = p;
                while (p < format.size() && std::isdigit(static_cast<unsigned char>(format[p]))) {
                    ++p;
                }
                out += groupValue(std::stoi(std::string(format.substr(start, p - start))));
                pos = p;
                continue;
            }
            if (p < format.size() && format[p] == '{') {
                ++p;
                const std::size_t digitsStart = p;
                while (p < format.size() && std::isdigit(static_cast<unsigned char>(format[p]))) {
                    ++p;
                }
                if (p == digitsStart) {
                    out.push_back('$');
                    ++pos;
                    continue;
                }
                const int n = std::stoi(std::string(format.substr(digitsStart, p - digitsStart)));
                if (p < format.size() && format[p] == '}') {
                    out += groupValue(n);
                    pos = p + 1;
                    continue;
                }
                if (p < format.size() && format[p] == ':') {
                    ++p;
                    if (p < format.size() && format[p] == '/') {
                        ++p;
                        const std::size_t modStart = p;
                        while (p < format.size() && format[p] != '}') {
                            ++p;
                        }
                        if (p >= format.size()) {
                            out.push_back('$');
                            ++pos;
                            continue;
                        }
                        out += ApplyCase(groupValue(n), format.substr(modStart, p - modStart));
                        pos = p + 1;
                        continue;
                    }
                    bool conditional = false; // ':+if' -- only when the group participated
                    if (p < format.size() && format[p] == '+') {
                        conditional = true;
                        ++p;
                    }
                    else if (p < format.size() && format[p] == '-') {
                        ++p; // ':-else' -- explicit else marker, same handling as the bare ':else' form below
                    }
                    std::string text;
                    while (p < format.size() && format[p] != '}') {
                        if (format[p] == '\\' && p + 1 < format.size() &&
                            (format[p + 1] == '}' || format[p + 1] == '$' || format[p + 1] == '\\')) {
                            text.push_back(format[p + 1]);
                            p += 2;
                            continue;
                        }
                        text.push_back(format[p]);
                        ++p;
                    }
                    if (p >= format.size()) {
                        out.push_back('$');
                        ++pos;
                        continue;
                    }
                    if (conditional) {
                        if (groupParticipated(n)) {
                            out += text;
                        }
                    }
                    else {
                        out += groupParticipated(n) ? groupValue(n) : text;
                    }
                    pos = p + 1;
                    continue;
                }
            }
            out.push_back('$'); // ill-formed -- literal '$'
            ++pos;
        }
        return out;
    }

    // Applies transform to fieldText -- the live half of a tabstop mirror's
    // /regex/format/flags, and the one-shot half of a variable's own. A bad
    // pattern (or a match-limit trip, RegexPattern's own safety net) never
    // loses the whole snippet session -- it degrades to the untransformed
    // text, the same tolerance every other ill-formed construct in this
    // file gets.
    std::string ApplyTransform(std::string_view fieldText, const SnippetTransform& transform) {
        try {
            const RegexPattern pattern(transform.pattern);
            std::string        result;
            std::size_t        cursor  = 0;
            std::size_t        lastEnd = 0;
            while (cursor <= fieldText.size()) {
                const auto match = pattern.Search(fieldText, cursor);
                if (!match) {
                    break;
                }
                result += fieldText.substr(lastEnd, match->start - lastEnd);
                result += ExpandTransformFormat(transform.format, fieldText, *match);
                lastEnd = match->end;
                cursor  = match->end > match->start ? match->end : match->end + 1;
                if (!transform.global) {
                    break;
                }
            }
            result += fieldText.substr(std::min(lastEnd, fieldText.size()));
            return result;
        }
        catch (const RegexPatternError&) {
            return std::string(fieldText);
        }
    }

} // namespace

ParsedSnippet ParseSnippet(std::string_view body, const SnippetVariables& variables) {
    // Pass 1: tokenize into literal runs and tabstop markers.
    struct Piece {
        bool        isStop = false;
        std::string literal;
        RawStop     stop{};
    };
    std::vector<Piece> pieces;
    std::string        literal;
    const auto         flushLiteral = [&] {
        if (!literal.empty()) {
            pieces.push_back(Piece{false, std::move(literal), {}});
            literal.clear();
        }
    };
    std::size_t pos = 0;
    while (pos < body.size()) {
        const char c = body[pos];
        if (c == '\\' && pos + 1 < body.size() && (body[pos + 1] == '$' || body[pos + 1] == '\\')) {
            literal.push_back(body[pos + 1]);
            pos += 2;
            continue;
        }
        if (c == '$') {
            std::size_t probe = pos;
            if (auto variable = TryParseVariable(body, probe, variables)) {
                literal += *variable;
                pos = probe;
                continue;
            }
            probe = pos;
            if (auto stop = ParseTabstopAt(body, probe)) {
                flushLiteral();
                pieces.push_back(Piece{true, "", std::move(*stop)});
                pos = probe;
                continue;
            }
        }
        literal.push_back(c);
        ++pos;
    }
    flushLiteral();

    // Each index's substitution text: its first placeholder-carrying
    // occurrence wins; an index with no placeholder anywhere substitutes "".
    std::map<int, std::string> primaryText;
    std::map<int, std::size_t> primaryPiece; // piece position of the winning occurrence
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        if (!piece.isStop) {
            continue;
        }
        if (!primaryPiece.contains(piece.stop.index) || (piece.stop.hasPlaceholder && !pieces[primaryPiece[piece.stop.index]].stop.hasPlaceholder)) {
            primaryPiece[piece.stop.index] = i;
            primaryText[piece.stop.index]  = piece.stop.placeholder;
        }
    }

    // Pass 2: emit stripped text, recording every occurrence as a field.
    ParsedSnippet result;
    struct Emitted {
        SnippetField field;
        bool         primary;
        std::size_t  piecePos;
    };
    std::vector<Emitted> emitted;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        if (!piece.isStop) {
            result.text += piece.literal;
            continue;
        }
        // A transform-bearing occurrence's own displayed text is the
        // primary's current (here: initial) substitution run through its
        // transform, not the shared substitution verbatim -- every other
        // occurrence of this index is untouched by it.
        const std::string& primary      = primaryText[piece.stop.index];
        const std::string  substitution = piece.stop.transform ? ApplyTransform(primary, *piece.stop.transform) : primary;
        const std::size_t  start        = result.text.size();
        result.text += substitution;
        emitted.push_back(Emitted{SnippetField{piece.stop.index, start, result.text.size(), piece.stop.transform},
                                  primaryPiece[piece.stop.index] == i, i});
    }

    // Visit order: ascending index with 0 last; within an index the primary
    // occurrence first, then mirrors in document order (stable sort keeps
    // piece order for equal keys).
    std::stable_sort(emitted.begin(), emitted.end(), [](const Emitted& a, const Emitted& b) {
        const int keyA = a.field.index == 0 ? INT_MAX : a.field.index;
        const int keyB = b.field.index == 0 ? INT_MAX : b.field.index;
        if (keyA != keyB) {
            return keyA < keyB;
        }
        return a.primary && !b.primary;
    });
    result.fields.reserve(emitted.size() + 1);
    for (const Emitted& e : emitted) {
        result.fields.push_back(e.field);
    }
    if (std::none_of(result.fields.begin(), result.fields.end(),
                     [](const SnippetField& f) { return f.index == 0; })) {
        result.fields.push_back(SnippetField{0, result.text.size(), result.text.size()});
    }
    return result;
}

std::optional<SnippetSession> SnippetSession::Start(text::Buffer& buffer, std::string bufferName,
                                                    std::size_t replaceStart, std::size_t replaceEnd,
                                                    const ParsedSnippet& parsed) {
    buffer.BeginUndoGroup();
    if (replaceEnd > replaceStart) {
        buffer.DeleteRange(replaceStart, replaceEnd - replaceStart);
    }
    buffer.InsertAt(replaceStart, parsed.text);

    const bool onlyFinalStop =
        std::all_of(parsed.fields.begin(), parsed.fields.end(), [](const SnippetField& f) { return f.index == 0; });
    if (onlyFinalStop) {
        buffer.EndUndoGroup();
        buffer.SetPoint(replaceStart + parsed.fields.front().start);
        return std::nullopt;
    }

    SnippetSession session;
    session.bufferName_ = std::move(bufferName);

    std::vector<text::Buffer::SnippetRange> ranges;
    ranges.reserve(parsed.fields.size());
    std::size_t nextId = 1;
    for (const SnippetField& field : parsed.fields) {
        const std::size_t id = nextId++;
        ranges.push_back(text::Buffer::SnippetRange{id, field.index, replaceStart + field.start,
                                                    replaceStart + field.end, false});
        if (field.transform) {
            session.transforms_[id] = *field.transform;
        }
        // parsed.fields is already in visit order (ascending index, 0 last)
        if (session.visitOrder_.empty() || session.visitOrder_.back() != field.index) {
            session.visitOrder_.push_back(field.index);
        }
    }
    buffer.SetSnippetRanges(std::move(ranges));

    session.activePos_ = 0;
    session.EnterActiveField(buffer);
    buffer.EndUndoGroup();
    return session;
}

void SnippetSession::EnterActiveField(text::Buffer& buffer) {
    const int index = visitOrder_[activePos_];
    // The index's primary range is the first one carrying it --
    // SetSnippetRanges preserved ParsedSnippet::fields' primary-first
    // order, and relocation never reorders the vector.
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.tabstopIndex == index) {
            activeRangeId_ = range.id;
            buffer.SetActiveSnippetRange(range.id);
            buffer.SetPoint(range.end);
            pristine_       = range.end > range.start;
            lastSyncedText_ = buffer.Content().Substring(range.start, range.end - range.start);
            return;
        }
    }
}

SnippetSession::NavResult SnippetSession::NextField(text::Buffer& buffer) {
    if (activePos_ + 1 >= visitOrder_.size()) {
        return NavResult::Finished;
    }
    ++activePos_;
    if (visitOrder_[activePos_] == 0) {
        for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
            if (range.tabstopIndex == 0) {
                buffer.SetPoint(range.start);
                break;
            }
        }
        return NavResult::Finished;
    }
    EnterActiveField(buffer);
    return NavResult::Moved;
}

SnippetSession::NavResult SnippetSession::PreviousField(text::Buffer& buffer) {
    if (activePos_ == 0) {
        return NavResult::Moved; // already at the first field -- stay
    }
    --activePos_;
    EnterActiveField(buffer);
    return NavResult::Moved;
}

void SnippetSession::Finish(text::Buffer& buffer) {
    buffer.ClearSnippetRanges();
}

bool SnippetSession::Pristine() const {
    return pristine_;
}

void SnippetSession::ClearPristine() {
    pristine_ = false;
}

void SnippetSession::DeleteActiveFieldContent(text::Buffer& buffer) {
    const text::Buffer::SnippetRange* range = FindRange(buffer, activeRangeId_);
    if (range != nullptr && range->end > range->start) {
        buffer.DeleteRange(range->start, range->end - range->start);
    }
}

void SnippetSession::SyncMirrors(text::Buffer& buffer) {
    const text::Buffer::SnippetRange* active = FindRange(buffer, activeRangeId_);
    if (active == nullptr) {
        return;
    }
    const std::string current = buffer.Content().Substring(active->start, active->end - active->start);
    if (current == lastSyncedText_) {
        return;
    }
    const int                index = active->tabstopIndex;
    std::vector<std::size_t> mirrorIds;
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.tabstopIndex == index && range.id != activeRangeId_) {
            mirrorIds.push_back(range.id);
        }
    }
    // A rewrite of a mirror directly adjacent to the active field inserts
    // exactly at point (the field's own edge), and Point_'s right-gravity
    // relocation would drag point to the end of the mirror's fresh text --
    // remember where point sits relative to the active field and restore
    // it after the rewrites. Point outside the field (the user moved away
    // mid-session) just rides ordinary relocation instead.
    const std::size_t pointBefore      = buffer.Point();
    const bool        pointInActive    = pointBefore >= active->start && pointBefore <= active->end;
    const std::size_t pointFieldOffset = pointInActive ? pointBefore - active->start : 0;
    // Deactivate for the rewrites: the active range's grow-at-boundary
    // gravity would absorb a rewrite of a directly adjacent mirror (the
    // insert lands exactly at the active field's own edge); with every
    // range inactive, boundary inserts stay excluded everywhere and each
    // rewritten mirror is repaired explicitly below.
    buffer.SetActiveSnippetRange(0); // 0 is never an assigned id -- clears every flag
    for (const std::size_t id : mirrorIds) {
        // Re-resolve per iteration: each rewrite relocates every other range.
        const text::Buffer::SnippetRange* mirror = FindRange(buffer, id);
        if (mirror == nullptr) {
            continue;
        }
        const std::size_t start = mirror->start;
        // A mirror carrying its own /regex/format/flags shows the
        // transformed content, recomputed fresh from the primary's current
        // text on every sync -- every other (plain) mirror shows it verbatim.
        const auto        transformIt = transforms_.find(id);
        const std::string replacement = transformIt != transforms_.end() ? ApplyTransform(current, transformIt->second) : current;
        if (buffer.Content().Substring(start, mirror->end - start) == replacement) {
            continue;
        }
        if (mirror->end > start) {
            buffer.DeleteRange(start, mirror->end - start);
        }
        if (!replacement.empty()) {
            buffer.InsertAt(start, replacement);
        }
        buffer.UpdateSnippetRange(id, start, start + replacement.size());
    }
    buffer.SetActiveSnippetRange(activeRangeId_);
    if (pointInActive) {
        if (const text::Buffer::SnippetRange* after = FindRange(buffer, activeRangeId_)) {
            buffer.SetPoint(after->start + pointFieldOffset);
        }
    }
    lastSyncedText_ = current;
}

bool SnippetSession::RangesValid(const text::Buffer& buffer) const {
    return !buffer.SnippetRanges().empty();
}

std::optional<std::pair<std::size_t, std::size_t>> SnippetSession::ActiveFieldRange(const text::Buffer& buffer) const {
    const text::Buffer::SnippetRange* range = FindRange(buffer, activeRangeId_);
    if (range == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(range->start, range->end);
}

const std::string& SnippetSession::BufferName() const {
    return bufferName_;
}

std::string SnippetSession::StatusText() const {
    const std::size_t total = visitOrder_.empty() ? 0 : visitOrder_.size() - 1; // the final stop isn't a field
    return "Snippet field " + std::to_string(activePos_ + 1) + "/" + std::to_string(total) + " (TAB next, S-TAB previous, ESC done)";
}

const text::Buffer::SnippetRange* SnippetSession::FindRange(const text::Buffer& buffer, std::size_t id) const {
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.id == id) {
            return &range;
        }
    }
    return nullptr;
}

} // namespace ned::editor
