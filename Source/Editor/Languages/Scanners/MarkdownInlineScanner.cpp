// The markdown-inline external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-markdown (src/scanner.c, MIT
// license) to ned's scanner interface. Code spans, LaTeX spans and
// strikethrough keep the upstream algorithm. Emphasis is ned's own: CommonMark
// matches `*`/`_` runs against a stack of openers after reading them, so each
// run is decided here the same way -- a closer against the openers on a stack
// kept in the scanner's state, an opener by simulating the rest of the inline
// text to see whether anything will close it.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ned::editor::languages::scanners::markdown_inline {

using namespace ned::editor::parse::scanner;

#ifdef _MSC_VER
#define UNUSED __pragma(warning(suppress : 4101))
#else
#define UNUSED __attribute__((unused))
#endif

// For explanation of the tokens see grammar.js
typedef enum {
    ERROR,
    TRIGGER_ERROR,
    CODE_SPAN_START,
    CODE_SPAN_CLOSE,
    EMPHASIS_OPEN_STAR,
    EMPHASIS_OPEN_UNDERSCORE,
    EMPHASIS_CLOSE_STAR,
    EMPHASIS_CLOSE_UNDERSCORE,
    LAST_TOKEN_WHITESPACE,
    LAST_TOKEN_PUNCTUATION,
    STRIKETHROUGH_OPEN,
    STRIKETHROUGH_CLOSE,
    LATEX_SPAN_START,
    LATEX_SPAN_CLOSE,
    UNCLOSED_SPAN,
    EMPHASIS_TEXT
} TokenType;

// Determines if a character is punctuation as defined by the markdown spec.
static bool is_punctuation(int32_t chr) {
    return (chr >= '!' && chr <= '/') || (chr >= ':' && chr <= '@') ||
           (chr >= '[' && chr <= '`') || (chr >= '{' && chr <= '~');
}

// State bitflags used with `Scanner.state`

// TODO
static UNUSED const uint8_t STATE_EMPHASIS_DELIMITER_MOD_3 = 0x3;
// Current delimiter run is opening
static const uint8_t STATE_EMPHASIS_DELIMITER_IS_OPEN = 0x1 << 2;

// Convenience function to emit the error token. This is done to stop invalid
// parse branches. Specifically:
// 1. When encountering a newline after a line break that ended a paragraph, and
// no new block
//    has been opened.
// 2. When encountering a new block after a soft line break.
// 3. When a `$._trigger_error` token is valid, which is used to stop parse
// branches through
//    normal tree-sitter grammar rules.
//
// See also the `$._soft_line_break` and `$._paragraph_end_newline` tokens in
// grammar.js
static bool error(Lexer* lexer) {
    lexer->resultSymbol = ERROR;
    return true;
}

// An emphasis opener still waiting for its closer: which character, how many
// of its characters are left to match, and what CommonMark's rule of three
// needs to know about the run it came from.
typedef struct {
    uint8_t chr;
    uint8_t count;
    uint8_t length; // the whole run's length, capped at 255
    uint8_t can_close;
} Opener;

static const uint8_t kMaxOpeners = 32;

typedef struct {
    // Parser state flags
    uint8_t state;
    uint8_t code_span_delimiter_length;
    uint8_t latex_span_delimiter_length;
    // The number of characters remaining in the current strikethrough
    // delimiter run.
    uint8_t num_emphasis_delimiters_left;
    // What the rest of the current `*`/`_` run emits, in this order.
    uint8_t run_chr;
    uint8_t close_left;
    uint8_t text_left;
    uint8_t open_left;
    uint8_t opener_count;
    Opener  openers[kMaxOpeners];
} Scanner;

// Write the whole state of a Scanner to a byte buffer
static unsigned serialize(Scanner* s, char* buffer) {
    unsigned size  = 0;
    buffer[size++] = (char)s->state;
    buffer[size++] = (char)s->code_span_delimiter_length;
    buffer[size++] = (char)s->latex_span_delimiter_length;
    buffer[size++] = (char)s->num_emphasis_delimiters_left;
    buffer[size++] = (char)s->run_chr;
    buffer[size++] = (char)s->close_left;
    buffer[size++] = (char)s->text_left;
    buffer[size++] = (char)s->open_left;
    buffer[size++] = (char)s->opener_count;
    memcpy(buffer + size, s->openers, s->opener_count * sizeof(Opener));
    size += s->opener_count * sizeof(Opener);
    return size;
}

// Read the whole state of a Scanner from a byte buffer
// `serizalize` and `deserialize` should be fully symmetric.
static void deserialize(Scanner* s, const char* buffer, unsigned length) {
    memset(s, 0, sizeof(Scanner));
    if (length > 0) {
        size_t size                     = 0;
        s->state                        = (uint8_t)buffer[size++];
        s->code_span_delimiter_length   = (uint8_t)buffer[size++];
        s->latex_span_delimiter_length  = (uint8_t)buffer[size++];
        s->num_emphasis_delimiters_left = (uint8_t)buffer[size++];
        s->run_chr                      = (uint8_t)buffer[size++];
        s->close_left                   = (uint8_t)buffer[size++];
        s->text_left                    = (uint8_t)buffer[size++];
        s->open_left                    = (uint8_t)buffer[size++];
        s->opener_count                 = (uint8_t)buffer[size++];
        memcpy(s->openers, buffer + size, s->opener_count * sizeof(Opener));
    }
}

static bool parse_leaf_delimiter(Lexer* lexer, uint8_t* delimiter_length,
                                 const bool*     valid_symbols,
                                 const char      delimiter,
                                 const TokenType open_token,
                                 const TokenType close_token) {
    uint8_t level = 0;
    while (lexer->lookahead == delimiter) {
        lexer->advance(lexer, false);
        level++;
    }
    lexer->markEnd(lexer);
    if (level == *delimiter_length && valid_symbols[close_token]) {
        *delimiter_length   = 0;
        lexer->resultSymbol = close_token;
        return true;
    }
    if (valid_symbols[open_token]) {
        // Parse ahead to check if there is a closing delimiter
        size_t close_level = 0;
        while (!lexer->eof(lexer)) {
            if (lexer->lookahead == delimiter) {
                close_level++;
            }
            else {
                if (close_level == level) {
                    // Found a matching delimiter
                    break;
                }
                close_level = 0;
            }
            lexer->advance(lexer, false);
        }
        if (close_level == level) {
            *delimiter_length   = level;
            lexer->resultSymbol = open_token;
            return true;
        }
        if (valid_symbols[UNCLOSED_SPAN]) {
            lexer->resultSymbol = UNCLOSED_SPAN;
            return true;
        }
    }
    return false;
}

static bool parse_backtick(Scanner* s, Lexer* lexer,
                           const bool* valid_symbols) {
    return parse_leaf_delimiter(lexer, &s->code_span_delimiter_length,
                                valid_symbols, '`', CODE_SPAN_START,
                                CODE_SPAN_CLOSE);
}

static bool parse_dollar(Scanner* s, Lexer* lexer,
                         const bool* valid_symbols) {
    return parse_leaf_delimiter(lexer, &s->latex_span_delimiter_length,
                                valid_symbols, '$', LATEX_SPAN_START,
                                LATEX_SPAN_CLOSE);
}

static bool parse_tilde(Scanner* s, Lexer* lexer, const bool* valid_symbols) {
    lexer->advance(lexer, false);
    // If `num_emphasis_delimiters_left` is not zero then we already decided
    // that this should be part of an emphasis delimiter run, so interpret it as
    // such.
    if (s->num_emphasis_delimiters_left > 0) {
        // The `STATE_EMPHASIS_DELIMITER_IS_OPEN` state flag tells us wether it
        // should be open or close.
        if ((s->state & STATE_EMPHASIS_DELIMITER_IS_OPEN) &&
            valid_symbols[STRIKETHROUGH_OPEN]) {
            s->state &= (~STATE_EMPHASIS_DELIMITER_IS_OPEN);
            lexer->resultSymbol = STRIKETHROUGH_OPEN;
            s->num_emphasis_delimiters_left--;
            return true;
        }
        if (valid_symbols[STRIKETHROUGH_CLOSE]) {
            lexer->resultSymbol = STRIKETHROUGH_CLOSE;
            s->num_emphasis_delimiters_left--;
            return true;
        }
    }
    lexer->markEnd(lexer);
    // Otherwise count the number of tildes
    uint8_t star_count = 1;
    while (lexer->lookahead == '~') {
        star_count++;
        lexer->advance(lexer, false);
    }
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r' ||
                    lexer->eof(lexer);
    if (valid_symbols[STRIKETHROUGH_OPEN] ||
        valid_symbols[STRIKETHROUGH_CLOSE]) {
        // The desicion made for the first star also counts for all the
        // following stars in the delimiter run. Rembemer how many there are.
        s->num_emphasis_delimiters_left = star_count - 1;
        // Look ahead to the next symbol (after the last star) to find out if it
        // is whitespace punctuation or other.
        bool next_symbol_whitespace =
            line_end || lexer->lookahead == ' ' || lexer->lookahead == '\t';
        bool next_symbol_punctuation = is_punctuation(lexer->lookahead);
        // Information about the last token is in valid_symbols. See grammar.js
        // for these tokens for how this is done.
        if (valid_symbols[STRIKETHROUGH_CLOSE] &&
            !valid_symbols[LAST_TOKEN_WHITESPACE] &&
            (!valid_symbols[LAST_TOKEN_PUNCTUATION] ||
             next_symbol_punctuation || next_symbol_whitespace)) {
            // Closing delimiters take precedence
            s->state &= ~STATE_EMPHASIS_DELIMITER_IS_OPEN;
            lexer->resultSymbol = STRIKETHROUGH_CLOSE;
            return true;
        }
        if (!next_symbol_whitespace && (!next_symbol_punctuation ||
                                        valid_symbols[LAST_TOKEN_PUNCTUATION] ||
                                        valid_symbols[LAST_TOKEN_WHITESPACE])) {
            s->state |= STATE_EMPHASIS_DELIMITER_IS_OPEN;
            lexer->resultSymbol = STRIKETHROUGH_OPEN;
            return true;
        }
    }
    return false;
}

// Unicode whitespace as CommonMark means it; 0 stands for the start or end
// of the text, which counts as whitespace.
static bool is_unicode_whitespace(int32_t chr) {
    return chr == 0 || chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r' || chr == '\f' || chr == 0xA0 ||
           chr == 0x1680 || (chr >= 0x2000 && chr <= 0x200A) || chr == 0x202F || chr == 0x205F || chr == 0x3000;
}

// ASCII punctuation plus the common Unicode punctuation blocks (GFM counts
// the P* general categories).
static bool is_unicode_punctuation(int32_t chr) {
    return is_punctuation(chr) || chr == 0xA1 || chr == 0xA7 || chr == 0xAB || chr == 0xB6 || chr == 0xB7 ||
           chr == 0xBB || chr == 0xBF || (chr >= 0x2010 && chr <= 0x2027) || (chr >= 0x2030 && chr <= 0x205E) ||
           (chr >= 0x3001 && chr <= 0x3003) || (chr >= 0x3008 && chr <= 0x3011) ||
           (chr >= 0x3014 && chr <= 0x301F) || (chr >= 0xFF01 && chr <= 0xFF0F && chr != 0xFF04 && chr != 0xFF0B) ||
           (chr >= 0xFF1A && chr <= 0xFF20 && chr != 0xFF1C && chr != 0xFF1D && chr != 0xFF1E) ||
           (chr >= 0xFF3B && chr <= 0xFF3D) || chr == 0xFF3F || chr == 0xFF5B || chr == 0xFF5D ||
           (chr >= 0xFF5F && chr <= 0xFF65);
}

// A delimiter run as CommonMark's flanking rules classify it.
typedef struct {
    uint8_t chr;
    int     length;
    bool    can_open;
    bool    can_close;
} Run;

static Run classify_run(int32_t chr, int length, int32_t before, int32_t after) {
    const bool before_space = is_unicode_whitespace(before);
    const bool after_space  = is_unicode_whitespace(after);
    const bool before_punct = is_unicode_punctuation(before);
    const bool after_punct  = is_unicode_punctuation(after);
    const bool left         = !after_space && (!after_punct || before_space || before_punct);
    const bool right        = !before_space && (!before_punct || after_space || after_punct);
    Run        run          = {(uint8_t)chr, length, left, right};
    if (chr == '_') {
        run.can_open  = left && (!right || before_punct);
        run.can_close = right && (!left || after_punct);
    }
    return run;
}

// Matches `closer` against `openers[bottom..count)` the way CommonMark's
// "process emphasis" does: nearest usable opener first, two characters at a
// time when both sides have two, dropping every opener between the pair.
// Returns how many of the closer's characters were used; `matched_at`, when
// given, is credited with what the opener at that index received.
static int close_run(Opener* openers, uint8_t* count, uint8_t bottom, Run closer, int* remaining,
                     int matched_at, int* matched) {
    int used = 0;
    while (*remaining > 0) {
        int found = -1;
        for (int i = (int)*count - 1; i >= (int)bottom; i--) {
            const Opener& o = openers[i];
            if (o.chr != closer.chr || o.count == 0)
                continue;
            // The rule of three.
            if ((o.can_close || closer.can_open) && (o.length + closer.length) % 3 == 0 &&
                !(o.length % 3 == 0 && closer.length % 3 == 0))
                continue;
            found = i;
            break;
        }
        if (found < 0)
            break;
        const int take = (openers[found].count >= 2 && *remaining >= 2) ? 2 : 1;
        openers[found].count -= (uint8_t)take;
        *remaining -= take;
        used += take;
        if (found == matched_at && matched != nullptr)
            *matched += take;
        *count = (uint8_t)(found + (openers[found].count > 0 ? 1 : 0));
    }
    return used;
}

static const size_t kMaxSimulatedCodepoints = 4096;

// The text after the run, read from the lexer only as far as the simulation
// looks, up to the budget.
struct Ahead {
    Lexer*               lexer;
    std::vector<int32_t> text;

    // Whether index i exists, reading up to it.
    bool has(size_t i) {
        while (text.size() <= i && text.size() < kMaxSimulatedCodepoints && !lexer->eof(lexer)) {
            text.push_back(lexer->lookahead);
            lexer->advance(lexer, false);
        }
        return i < text.size();
    }
    int32_t operator[](size_t i) {
        return has(i) ? text[i] : 0;
    }
};

// Index just past a run of `chr` starting at `at`.
static size_t run_end(Ahead& text, size_t at, int32_t chr) {
    while (text.has(at) && text[at] == chr)
        at++;
    return at;
}

// Where a backtick or dollar span opened by `length` characters at `at`
// ends, or `at` itself when nothing closes it.
static size_t span_end(Ahead& text, size_t at, int32_t chr, size_t length) {
    for (size_t i = at; text.has(i);) {
        if (text[i] != chr) {
            i++;
            continue;
        }
        const size_t end = run_end(text, i, chr);
        if (end - i == length)
            return end;
        i = end;
    }
    return at;
}

// Whether the `[` at `at` has its `]`: only then is it link text.
static bool bracket_closes(Ahead& text, size_t at) {
    int nesting = 0;
    for (size_t i = at; text.has(i); i++) {
        if (text[i] == '\\')
            i++;
        else if (text[i] == '[')
            nesting++;
        else if (text[i] == ']' && --nesting == 0)
            return true;
    }
    return false;
}

// How many characters of the opener on top of `openers` (the run just read)
// something in `text` will close, running CommonMark's matching over it.
// Link text is its own scope, and code, LaTeX, autolinks, raw HTML and
// escaped characters hold no delimiters.
static int simulate(Opener* openers, uint8_t count, int32_t before, Ahead& text) {
    const int subject = (int)count - 1;
    int       matched = 0;
    uint8_t   scopes[kMaxOpeners];
    int       depth = 0;
    int32_t   prev  = before;
    for (size_t i = 0; text.has(i);) {
        const int32_t chr = text[i];
        if (chr == '\\' && text.has(i + 1) && is_punctuation(text[i + 1])) {
            prev = text[i + 1];
            i += 2;
        }
        else if (chr == '`' || chr == '$') {
            const size_t end   = run_end(text, i, chr);
            const size_t close = span_end(text, end, chr, end - i);
            i                  = close > end ? close : end;
            prev               = chr;
        }
        else if (chr == '<' && text.has(i + 1) &&
                 ((text[i + 1] >= 'a' && text[i + 1] <= 'z') || (text[i + 1] >= 'A' && text[i + 1] <= 'Z') ||
                  text[i + 1] == '/' || text[i + 1] == '!' || text[i + 1] == '?')) {
            size_t end = i + 1;
            while (text.has(end) && text[end] != '>')
                end++;
            const bool closed = text.has(end);
            i                 = closed ? end + 1 : i + 1;
            prev              = closed ? '>' : '<';
        }
        else if (chr == '[' && bracket_closes(text, i)) {
            if (depth < (int)kMaxOpeners)
                scopes[depth] = count;
            depth++;
            prev = chr;
            i++;
        }
        else if (chr == ']') {
            // A `]` nothing here opens, followed by a destination or a label,
            // ends link text the run itself sits in.
            if (depth == 0 && text.has(i + 1) && (text[i + 1] == '(' || text[i + 1] == '['))
                return matched;
            if (depth > 0) {
                depth--;
                if (depth < (int)kMaxOpeners && count > scopes[depth])
                    count = scopes[depth];
                if ((int)count <= subject)
                    return matched;
            }
            const bool link_text = depth > 0;
            i++;
            prev = chr;
            // A link destination holds no emphasis.
            if (link_text && text.has(i) && text[i] == '(') {
                int    nesting = 0;
                size_t end     = i;
                for (; text.has(end); end++) {
                    if (text[end] == '\\') {
                        end++;
                        continue;
                    }
                    if (text[end] == '(')
                        nesting++;
                    else if (text[end] == ')' && --nesting == 0)
                        break;
                }
                if (text.has(end)) {
                    i    = end + 1;
                    prev = ')';
                }
            }
        }
        else if (chr == '*' || chr == '_') {
            const size_t  end   = run_end(text, i, chr);
            const int32_t after = text.has(end) ? text[end] : 0;
            const Run     run   = classify_run(chr, (int)(end - i), prev, after);
            int           left  = run.length;
            if (run.can_close) {
                const uint8_t bottom = depth > 0 && depth <= (int)kMaxOpeners ? scopes[depth - 1] : 0;
                close_run(openers, &count, bottom, run, &left, subject, &matched);
                if ((int)count <= subject || openers[subject].count == 0)
                    return matched;
            }
            if (left > 0 && run.can_open && count < kMaxOpeners)
                openers[count++] = {(uint8_t)chr, (uint8_t)(left > 255 ? 255 : left),
                                    (uint8_t)(run.length > 255 ? 255 : run.length), run.can_close};
            prev = chr;
            i    = end;
        }
        else {
            prev = chr;
            i++;
        }
    }
    return matched;
}

// Plans a `*`/`_` run whose first character is the lookahead: how many of
// its characters close openers on the stack, how many open emphasis
// something later closes, and how many are plain text.
static void plan_run(Scanner* s, Lexer* lexer, const bool* valid_symbols, TokenType close_token) {
    const int32_t chr    = lexer->lookahead;
    const int32_t before = lexer->lookbehind(lexer);
    int           length = 0;
    while (lexer->lookahead == chr && !lexer->eof(lexer)) {
        lexer->advance(lexer, false);
        if (length == 0)
            lexer->markEnd(lexer);
        length++;
    }
    const int32_t after = lexer->eof(lexer) ? 0 : lexer->lookahead;
    const Run     run   = classify_run(chr, length, before, after);

    int left   = length;
    int closes = 0;
    if (run.can_close && valid_symbols[close_token])
        closes = close_run(s->openers, &s->opener_count, 0, run, &left, -1, nullptr);

    int opens = 0;
    if (left > 0 && run.can_open && s->opener_count < kMaxOpeners) {
        Opener openers[kMaxOpeners];
        memcpy(openers, s->openers, s->opener_count * sizeof(Opener));
        const Opener opener      = {(uint8_t)chr, (uint8_t)(left > 255 ? 255 : left),
                                    (uint8_t)(length > 255 ? 255 : length), run.can_close};
        openers[s->opener_count] = opener;
        Ahead ahead{lexer, {}};
        opens = simulate(openers, s->opener_count + 1, chr, ahead);
        if (opens > 0) {
            s->openers[s->opener_count]       = opener;
            s->openers[s->opener_count].count = (uint8_t)opens;
            s->opener_count++;
        }
    }
    s->run_chr    = (uint8_t)chr;
    s->close_left = (uint8_t)(closes > 255 ? 255 : closes);
    s->open_left  = (uint8_t)(opens > 255 ? 255 : opens);
    s->text_left  = (uint8_t)(length - closes - opens > 255 ? 255 : length - closes - opens);
}

// Emits the next character of the planned run as its token.
static bool emit_planned(Scanner* s, Lexer* lexer, const bool* valid_symbols, TokenType open_token,
                         TokenType close_token) {
    TokenType token = EMPHASIS_TEXT;
    if (s->close_left > 0) {
        s->close_left--;
        token = close_token;
    }
    else if (s->text_left > 0) {
        s->text_left--;
    }
    else if (s->open_left > 0) {
        s->open_left--;
        token = open_token;
    }
    if (!valid_symbols[token])
        token = EMPHASIS_TEXT;
    if (!valid_symbols[token])
        return false;
    lexer->resultSymbol = token;
    return true;
}

static bool parse_emphasis(Scanner* s, Lexer* lexer, const bool* valid_symbols, TokenType open_token,
                           TokenType close_token) {
    const int32_t chr     = lexer->lookahead;
    const bool    planned = s->run_chr == chr && s->close_left + s->text_left + s->open_left > 0 &&
                            lexer->lookbehind(lexer) == chr;
    if (!planned) {
        if (!valid_symbols[open_token] && !valid_symbols[close_token] && !valid_symbols[EMPHASIS_TEXT])
            return false;
        plan_run(s, lexer, valid_symbols, close_token);
        return emit_planned(s, lexer, valid_symbols, open_token, close_token);
    }
    lexer->advance(lexer, false);
    lexer->markEnd(lexer);
    return emit_planned(s, lexer, valid_symbols, open_token, close_token);
}
static bool scan(Scanner* s, Lexer* lexer, const bool* valid_symbols) {
    // A normal tree-sitter rule decided that the current branch is invalid and
    // now "requests" an error to stop the branch
    if (valid_symbols[TRIGGER_ERROR]) {
        return error(lexer);
    }

    // Decide which tokens to consider based on the first non-whitespace
    // character
    switch (lexer->lookahead) {
        case '`':
            // A backtick could mark the beginning or ending of a code span or a
            // fenced code block.
            return parse_backtick(s, lexer, valid_symbols);
        case '$':
            return parse_dollar(s, lexer, valid_symbols);
        case '*':
            // A star could either mark the beginning or ending of emphasis, a
            // list item or thematic break. This code is similar to the code for
            // '_' and '+'.
            return parse_emphasis(s, lexer, valid_symbols, EMPHASIS_OPEN_STAR, EMPHASIS_CLOSE_STAR);
        case '_':
            return parse_emphasis(s, lexer, valid_symbols, EMPHASIS_OPEN_UNDERSCORE, EMPHASIS_CLOSE_UNDERSCORE);
        case '~':
            return parse_tilde(s, lexer, valid_symbols);
    }
    return false;
}

void* Create() {
    Scanner* s = (Scanner*)scanner_malloc(sizeof(Scanner));
    deserialize(s, NULL, 0);
    return s;
}

static bool Scan(
    void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    return scan(scanner, lexer, valid_symbols);
}

static unsigned Serialize(void* payload_,
                          char* buffer) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    return serialize(scanner, buffer);
}

static void Deserialize(void*       payload_,
                        const char* buffer,
                        unsigned    length) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    deserialize(scanner, buffer, length);
}

static void Destroy(void* payload_) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::markdown_inline
