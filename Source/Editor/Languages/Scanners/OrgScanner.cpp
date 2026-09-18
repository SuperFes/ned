// The org external scanner, ported from https://github.com/SuperFes/tree-sitter-ned-org (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::org {

using namespace ned::editor::parse::scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VEC_RESIZE(vec, _cap)                                                        \
    {                                                                                \
        (vec)->data = scanner_realloc((vec)->data, (_cap) * sizeof((vec)->data[0])); \
        assert((vec)->data != NULL);                                                 \
        (vec)->cap = (_cap);                                                         \
    }

#define VEC_PUSH(vec, el)                               \
    {                                                   \
        if ((vec)->cap == (vec)->len) {                 \
            VEC_RESIZE((vec), MAX(16, (vec)->len * 2)); \
        }                                               \
        (vec)->data[(vec)->len++] = (el);               \
    }

#define VEC_POP(vec) (vec)->len--;

#define VEC_BACK(vec) ((vec)->data[(vec)->len - 1])

#define VEC_FREE(vec)            \
    {                            \
        if ((vec)->data != NULL) \
            free((vec)->data);   \
    }

#define VEC_CLEAR(vec)  \
    {                   \
        (vec)->len = 0; \
    }

enum TokenType {
    LISTSTART,
    LISTEND,
    LISTITEMEND,
    BULLET,
    HLSTARS,
    SECTIONEND,
    ENDOFFILE,

    // Ned's own addition -- order must exactly match grammar.js's own
    // `externals` array (tree-sitter assigns external token IDs by position
    // in that array; see scan_emphasis's own comment for the actual rule
    // each open/close token implements).
    BOLD_OPEN,
    BOLD_CLOSE,
    ITALIC_OPEN,
    ITALIC_CLOSE,
    UNDERLINE_OPEN,
    UNDERLINE_CLOSE,
    VERBATIM_OPEN,
    VERBATIM_CLOSE,
    CODE_OPEN,
    CODE_CLOSE,
    STRIKETHROUGH_OPEN,
    STRIKETHROUGH_CLOSE,
};

typedef enum {
    NOTABULLET,
    DASH,
    PLUS,
    STAR,
    LOWERDOT,
    UPPERDOT,
    LOWERPAREN,
    UPPERPAREN,
    NUMDOT,
    NUMPAREN,
} Bullet;

typedef struct {
    uint32_t len;
    uint32_t cap;
    int16_t* data;
} stack;

typedef struct {
    stack* indent_length_stack;
    stack* bullet_stack;
    stack* section_stack;
} Scanner;

static inline void advance(Lexer* lexer) {
    lexer->advance(lexer, false);
}

static inline void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}

unsigned serialize(Scanner* scanner, char* buffer) {
    size_t i = 0;

    size_t indent_count = scanner->indent_length_stack->len - 1;
    if (indent_count > UINT8_MAX)
        indent_count = UINT8_MAX;
    buffer[i++] = indent_count;

    int iter = 1;
    for (; iter < scanner->indent_length_stack->len &&
           i < kSerializationBufferSize;
         ++iter) {
        buffer[i++] = scanner->indent_length_stack->data[iter];
    }

    iter = 1;
    for (; iter < scanner->bullet_stack->len &&
           i < kSerializationBufferSize;
         ++iter) {
        buffer[i++] = scanner->bullet_stack->data[iter];
    }

    iter = 1;
    for (; iter < scanner->section_stack->len &&
           i < kSerializationBufferSize;
         ++iter) {
        buffer[i++] = scanner->section_stack->data[iter];
    }

    return i;
}

void deserialize(Scanner* scanner, const char* buffer, unsigned length) {
    VEC_CLEAR(scanner->section_stack);
    VEC_PUSH(scanner->section_stack, 0);
    VEC_CLEAR(scanner->indent_length_stack);
    VEC_PUSH(scanner->indent_length_stack, -1);
    VEC_CLEAR(scanner->bullet_stack);
    VEC_PUSH(scanner->bullet_stack, NOTABULLET);

    if (length == 0)
        return;

    size_t i = 0;

    size_t indent_count = (uint8_t)buffer[i++];

    for (; i <= indent_count; i++)
        VEC_PUSH(scanner->indent_length_stack, buffer[i]);
    for (; i <= 2 * indent_count; i++)
        VEC_PUSH(scanner->bullet_stack, buffer[i]);
    for (; i < length; i++)
        VEC_PUSH(scanner->section_stack, buffer[i]);
}

static bool dedent(Scanner* scanner, Lexer* lexer) {
    VEC_POP(scanner->indent_length_stack);
    VEC_POP(scanner->bullet_stack);
    lexer->resultSymbol = LISTEND;
    return true;
}

static bool in_error_recovery(const bool* valid_symbols) {
    return (valid_symbols[LISTSTART] && valid_symbols[LISTEND] &&
            valid_symbols[LISTITEMEND] && valid_symbols[BULLET] &&
            valid_symbols[HLSTARS] && valid_symbols[SECTIONEND] &&
            valid_symbols[ENDOFFILE]);
}

Bullet getbullet(Lexer* lexer) {
    if (lexer->lookahead == '-') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return DASH;
    }
    else if (lexer->lookahead == '+') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return PLUS;
    }
    else if (lexer->lookahead == '*') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return STAR;
    }
    else if ('a' <= lexer->lookahead && lexer->lookahead <= 'z') {
        advance(lexer);
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return LOWERDOT;
        }
        else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return LOWERPAREN;
        }
    }
    else if ('A' <= lexer->lookahead && lexer->lookahead <= 'Z') {
        advance(lexer);
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return UPPERDOT;
        }
        else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return UPPERPAREN;
        }
    }
    else if ('0' <= lexer->lookahead && lexer->lookahead <= '9') {
        do {
            advance(lexer);
        }
        while ('0' <= lexer->lookahead && lexer->lookahead <= '9');
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return NUMDOT;
        }
        else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return NUMPAREN;
        }
    }
    return NOTABULLET;
}

// Ned's own addition -- see grammar.js's own `externals` comment for the
// overall design rationale (no backward character lookup needed, unlike
// tree-sitter-markdown-inline's LAST_TOKEN_WHITESPACE approach).
typedef struct {
    int32_t        marker;
    enum TokenType open;
    enum TokenType close;
} EmphasisMarker;

static const EmphasisMarker EMPHASIS_MARKERS[] = {
    {'*', BOLD_OPEN, BOLD_CLOSE},
    {'/', ITALIC_OPEN, ITALIC_CLOSE},
    {'_', UNDERLINE_OPEN, UNDERLINE_CLOSE},
    {'=', VERBATIM_OPEN, VERBATIM_CLOSE},
    {'~', CODE_OPEN, CODE_CLOSE},
    {'+', STRIKETHROUGH_OPEN, STRIKETHROUGH_CLOSE},
};
#define EMPHASIS_MARKER_COUNT (sizeof(EMPHASIS_MARKERS) / sizeof(EMPHASIS_MARKERS[0]))

// Real Org's own `org-emphasis-regexp-components` post-match component --
// the extra punctuation Org allows immediately after a closing marker,
// beyond plain whitespace/newline/EOF (each checked separately below).
static bool is_emphasis_post_punctuation(int32_t c) {
    switch (c) {
        case '-':
        case '.':
        case ',':
        case ';':
        case ':':
        case '!':
        case '?':
        case '\'':
        case '"':
        case ')':
        case ']':
        case '}':
        case '[':
            return true;
        default:
            return false;
    }
}

// Ned's own addition, not part of real Org's own post-match component: any
// OTHER emphasis marker character is also a valid boundary, both after a
// close (so nested emphasis can close back-to-back, e.g. italic's own
// close '/' immediately followed by the enclosing bold's own close '*' in
// "*bold /italic/*") and before an open (the mirror case, e.g. bold's own
// open '*' immediately followed by italic's own open '/' in "*/italic
// nested from the very start/*"). Without this, nested emphasis could open
// but could never find a valid close once its own closing marker happened
// to sit directly against another marker rather than whitespace/EOL -- a
// real, confirmed failure ("*bold /italic/*" producing a parse ERROR), not
// a hypothetical one.
static bool is_emphasis_marker_char(int32_t c) {
    for (size_t i = 0; i < EMPHASIS_MARKER_COUNT; i++) {
        if (c == EMPHASIS_MARKERS[i].marker) {
            return true;
        }
    }
    return false;
}

static bool is_emphasis_boundary(int32_t c) {
    return c == 0 || c == '\n' || c == '\r' || iswspace(c) || is_emphasis_post_punctuation(c) ||
           is_emphasis_marker_char(c);
}

// precededByWhitespace: whether this scan() invocation already skipped over
// at least one space/tab (its own leading indent-scan loop, run by the
// caller just above) before reaching lexer's current position. A real,
// load-bearing fix, not a style choice -- see this function's own call site
// for the full story: a bare early-in-scan() call here (tried first,
// abandoned) misses every marker preceded on the same line by earlier
// content + whitespace entirely, since that whitespace only ever gets
// skipped by the SEPARATE indent-scan loop, which runs (and, when it hits a
// non-space/tab byte, simply stops) without ever giving this function a
// second look at the position it stopped on.
//
// An OPEN marker is claimed only if (a) the very next character is not a
// boundary character (real Org's own "post-open must be non-blank" rule),
// AND (b) either precededByWhitespace or lexer->getColumn(lexer) == 0 --
// real Org's own "not mid-word" pre-open rule. (b) is an explicit check,
// not something grammar position alone guarantees for free: confirmed by a
// real failing test ("2*3" incorrectly opening bold), external tokens can
// still be offered by the parser immediately after an ordinary expr's own
// immediate-chained continuation in states this project hasn't been able
// to fully map by hand -- get_column()==0 covers genuine line-start (where
// nothing at all precedes, not even whitespace to have skipped), the one
// case precededByWhitespace alone can't distinguish from "touching a
// preceding word with zero gap." A CLOSE marker is claimed only if the very
// next character after it IS a boundary character, AND !precededByWhitespace
// -- real Org's own "immediately preceded by non-whitespace content" rule,
// which (unlike the open marker's rule) grammar.js's own structure doesn't
// enforce on its own for external tokens the way token.immediate does for
// internal ones.
//
// A '*' or '+' marker defers entirely (returns false immediately) whenever
// headline-stars/section-end (for '*') or liststart/bullet (for '+') are
// themselves valid symbols right now -- real Org headline stars and bold
// both use '*', and real Org list bullets and strikethrough both use '+';
// the structural use must always win. The Col=0 star block and the
// Liststart-and-bullets block (both run by the caller, below) already have
// everything in hand (star count / the already-scanned bullet shape) to
// correctly fall back to BOLD_OPEN/STRIKETHROUGH_OPEN themselves once
// they've confirmed NOT a valid headline/bullet -- see their own comments.
// Confirmed via a real failing test ("  - a\n  + a\n", an existing upstream
// corpus case, silently swallowing the second list's own "+" bullet as
// strikethrough instead of starting a new list), not assumed.
static bool scan_emphasis(Scanner* scanner, Lexer* lexer, const bool* valid_symbols, bool precededByWhitespace) {
    if (lexer->lookahead == '*' && (valid_symbols[HLSTARS] || valid_symbols[SECTIONEND])) {
        (void)scanner;
        return false;
    }
    if (lexer->lookahead == '+' && (valid_symbols[LISTSTART] || valid_symbols[BULLET])) {
        return false;
    }

    for (size_t i = 0; i < EMPHASIS_MARKER_COUNT; i++) {
        const EmphasisMarker* marker = &EMPHASIS_MARKERS[i];
        if (lexer->lookahead != marker->marker) {
            continue;
        }

        const bool notMidWord = precededByWhitespace || lexer->getColumn(lexer) == 0;
        const bool wantOpen   = valid_symbols[marker->open] && notMidWord;
        const bool wantClose  = valid_symbols[marker->close] && !precededByWhitespace;
        if (!wantOpen && !wantClose) {
            return false;
        }

        advance(lexer);

        if (wantOpen && !is_emphasis_boundary(lexer->lookahead)) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = marker->open;
            return true;
        }
        if (wantClose && is_emphasis_boundary(lexer->lookahead)) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = marker->close;
            return true;
        }
        return false;
    }
    return false;
}

bool scan(Scanner* scanner, Lexer* lexer, const bool* valid_symbols) {
    if (in_error_recovery(valid_symbols))
        return false;

    // - Section ends
    int16_t indent_length = 0;
    lexer->markEnd(lexer);
    for (;;) {
        if (lexer->lookahead == ' ') {
            indent_length++;
        }
        else if (lexer->lookahead == '\t') {
            indent_length += 8;
        }
        else if (lexer->lookahead == '\0') {
            if (valid_symbols[LISTEND]) {
                lexer->resultSymbol = LISTEND;
            }
            else if (valid_symbols[SECTIONEND]) {
                lexer->resultSymbol = SECTIONEND;
            }
            else if (valid_symbols[ENDOFFILE]) {
                lexer->resultSymbol = ENDOFFILE;
            }
            else
                return false;

            return true;
        }
        else {
            break;
        }
        skip(lexer);
    }

    // Ned's own addition: correctly positioned to see whatever the indent
    // scan above actually stopped on -- see scan_emphasis's own doc comment
    // for why this can't run any earlier (e.g. before the loop, at the very
    // top of scan()) without silently missing every marker preceded on its
    // own line by other content.
    if (scan_emphasis(scanner, lexer, valid_symbols, indent_length > 0))
        return true;

    // - Listiem ends
    // Listend -> end of a line, looking for:
    // 1. dedent
    // 2. same indent, not a bullet
    // 3. two eols
    int16_t newlines = 0;
    if (valid_symbols[LISTEND] || valid_symbols[LISTITEMEND]) {
        for (;;) {
            if (lexer->lookahead == ' ') {
                indent_length++;
            }
            else if (lexer->lookahead == '\t') {
                indent_length += 8;
            }
            else if (lexer->lookahead == '\0') {
                return dedent(scanner, lexer);
            }
            else if (lexer->lookahead == '\n') {
                if (++newlines > 1)
                    return dedent(scanner, lexer);
                indent_length = 0;
            }
            else {
                break;
            }
            skip(lexer);
        }

        if (indent_length < VEC_BACK(scanner->indent_length_stack)) {
            return dedent(scanner, lexer);
        }
        else if (indent_length == VEC_BACK(scanner->indent_length_stack)) {
            if (getbullet(lexer) == VEC_BACK(scanner->bullet_stack)) {
                lexer->resultSymbol = LISTITEMEND;
                return true;
            }
            return dedent(scanner, lexer);
        }
    }

    // - Col=0 star
    if (indent_length == 0 && lexer->lookahead == '*') {
        const uint32_t starColumn = lexer->getColumn(lexer); // captured before any advance() below -- see the BOLD_OPEN fallback's own use of it
        lexer->markEnd(lexer);
        int16_t stars = 1;
        // Ned's own change: advance(), not skip() -- skip()'d bytes can
        // never become part of a LATER mark_end()-reported token span (they
        // stay permanently invisible, treated as extras, regardless of what
        // happens afterward -- confirmed empirically, not assumed, while
        // adding the BOLD_OPEN fallback below). Harmless for the existing
        // HLSTARS/SECTIONEND paths either way: both already fix their own
        // token's end at the mark_end() call above, BEFORE any of this
        // consumption happens, so those stay zero-width exactly as before
        // regardless of skip() vs advance() here.
        advance(lexer);
        while (lexer->lookahead == '*') {
            stars++;
            advance(lexer);
        }

        if (lexer->lookahead == '\n') {
            return false;
        }

        if (valid_symbols[SECTIONEND] && iswspace(lexer->lookahead) &&
            stars > 0 && stars <= VEC_BACK(scanner->section_stack)) {
            VEC_POP(scanner->section_stack);
            lexer->resultSymbol = SECTIONEND;
            return true;
        }
        else if (valid_symbols[HLSTARS] && iswspace(lexer->lookahead)) {
            VEC_PUSH(scanner->section_stack, stars);
            lexer->resultSymbol = HLSTARS;
            return true;
        }
        // Ned's own addition: not a valid headline/section-end after all --
        // if it's a single star (not a multi-star run) immediately followed
        // by non-whitespace, it's a valid bold-open instead (see
        // scan_emphasis's own doc comment for the boundary rule). This has
        // to live here, not in scan_emphasis itself, run earlier: this
        // block already did the real exploration (stars counted, indent
        // confirmed to genuinely be 0) that a separate, earlier check would
        // otherwise have to unsafely redo -- see grammar.js's own
        // `externals` comment for why '*' is the one marker that can't be
        // decided by scan_emphasis alone. Entering this block at all already
        // means indent_length==0 (no whitespace precedes), so the only
        // remaining "not mid-word" check needed is genuine column 0 (of the
        // star itself, captured above before any advance() moved past it)
        // -- see scan_emphasis's own doc comment for why that's not
        // automatic (a real failing "2*3" test, not assumed).
        if (stars == 1 && valid_symbols[BOLD_OPEN] && !is_emphasis_boundary(lexer->lookahead) && starColumn == 0) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = BOLD_OPEN;
            return true;
        }
        return false;
    }

    // - Liststart and bullets
    if ((valid_symbols[LISTSTART] || valid_symbols[BULLET]) && newlines == 0) {
        const int32_t possibleBulletChar = lexer->lookahead; // captured before getbullet() advances past it
        Bullet        bullet             = getbullet(lexer);

        if (valid_symbols[BULLET] &&
            bullet == VEC_BACK(scanner->bullet_stack) &&
            indent_length == VEC_BACK(scanner->indent_length_stack)) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = BULLET;
            return true;
        }
        else if (valid_symbols[LISTSTART] && bullet != NOTABULLET &&
                 indent_length > VEC_BACK(scanner->indent_length_stack)) {
            VEC_PUSH(scanner->indent_length_stack, indent_length);
            VEC_PUSH(scanner->bullet_stack, bullet);
            lexer->resultSymbol = LISTSTART;
            return true;
        }
        // Ned's own addition: not a valid bullet after all -- if the
        // character getbullet() examined was specifically '+' and
        // STRIKETHROUGH_OPEN is wanted, it's a valid strikethrough-open
        // instead (getbullet() already advanced exactly past that one
        // character, mirroring the Col=0 star block's own BOLD_OPEN
        // fallback for the same reason -- see scan_emphasis's own doc
        // comment).
        if (bullet == NOTABULLET && possibleBulletChar == '+' && valid_symbols[STRIKETHROUGH_OPEN] &&
            !is_emphasis_boundary(lexer->lookahead)) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = STRIKETHROUGH_OPEN;
            return true;
        }
    }

    return false; // default
}

void* Create() {
    Scanner* scanner             = (Scanner*)scanner_calloc(1, sizeof(Scanner));
    scanner->indent_length_stack = (stack*)scanner_calloc(1, sizeof(stack));
    scanner->bullet_stack        = (stack*)scanner_calloc(1, sizeof(stack));
    scanner->section_stack       = (stack*)scanner_calloc(1, sizeof(stack));
    deserialize(scanner, NULL, 0);
    return scanner;
}

static bool Scan(void* payload_, Lexer* lexer,
                 const bool* valid_symbols) {
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
    VEC_FREE(scanner->indent_length_stack);
    VEC_FREE(scanner->bullet_stack);
    VEC_FREE(scanner->section_stack);
    free(scanner->indent_length_stack);
    free(scanner->bullet_stack);
    free(scanner->section_stack);
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::org
