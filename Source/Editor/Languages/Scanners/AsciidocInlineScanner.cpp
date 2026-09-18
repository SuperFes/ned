// The asciidoc_inline external scanner, ported from https://github.com/cathaysia/tree-sitter-asciidoc (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::asciidoc_inline {

using namespace ned::editor::parse::scanner;

// --- base_types.h (from the same grammar) ---

#ifndef u32
typedef uint32_t u32;
#endif

#ifndef i32
typedef int32_t i32;
#endif

#ifndef usize
typedef uintptr_t usize;
#endif

#ifndef isize
typedef intptr_t isize;
#endif

#ifndef USIZE_MAX
const usize USIZE_MAX = UINTPTR_MAX;
#endif

#ifndef ISIZE_MAX
const isize ISIZE_MAX = INTPTR_MAX;
#endif

typedef enum TokenType {
    TOKEN_TYPE_EOF,
    TOKEN_HARD_WRAP_PLUS,
    TOKEN_EMPHASIS_BEGIN,
    TOKEN_ITALIC_BEGIN,
    TOKEN_MONOSPACE_BEGIN,
    TOKEN_HIGHLIGHT_BEGIN,
} TokenType;

static bool is_space(int32_t c) {
    return c == ' ' || c == '\t';
}

static bool is_word_char(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// Scan a constrained opening delimiter `d` (the lexer is positioned at it).
//
// AsciiDoc constrained formatting only applies when the span is balanced.
// Emit the opening delimiter token only when:
//   - the opener is immediately followed by a non-space, non-delimiter char,
//   - and a valid closing `d` exists later on the same line: not escaped,
//     preceded by a non-space, and followed by a word boundary.
// Otherwise return false so the delimiter falls back to plain punctuation
// instead of error-recovering into a formatting node with a missing close.
static bool scan_constrained_begin(Lexer *lexer, int32_t d,
                                   TokenType symbol) {
    lexer->advance(lexer, false); // consume the opening delimiter
    int32_t after = lexer->lookahead;
    if (lexer->eof(lexer) || after == d || is_space(after) || after == '\n' ||
        after == '\r') {
        return false;
    }
    // The emitted token is exactly the opening delimiter.
    lexer->markEnd(lexer);

    // Look ahead for a valid close on the same line.
    int32_t prev = after;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != '\n' &&
           lexer->lookahead != '\r') {
        int32_t c = lexer->lookahead;
        if (c == d && prev != '\\' && !is_space(prev)) {
            lexer->advance(lexer, false);
            int32_t next = lexer->lookahead;
            // A doubled delimiter is the unconstrained form, not a close.
            if (next != d && (lexer->eof(lexer) || !is_word_char(next))) {
                lexer->resultSymbol = symbol;
                return true;
            }
            prev = c;
            continue;
        }
        prev = c;
        lexer->advance(lexer, false);
    }
    return false;
}

void *Create() {
    return NULL;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
    // ...
}

static unsigned Serialize(void *payload_,
                                                                char *buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(
    void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};
    // ...
}

static bool Scan(
    void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
    // Skip leading inline whitespace, kept out of any emitted token (the
    // `skip' flag), remembering whether we saw any -- a hard wrap needs a
    // preceding space.  Falling through (rather than bailing on spaces)
    // lets a constrained delimiter after a space still be recognized.
    // Spaces only count toward a hard wrap when they follow text on the
    // line; leading indentation (column 0) does not, so "    +" stays
    // punctuation while "text +" is a hard wrap.
    bool had_space = false;
    bool at_line_start = lexer->getColumn(lexer) == 0;
    for (;;) {
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            if (!at_line_start) {
                had_space = true;
            }
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            // A newline ends the run of spaces relevant to a hard wrap.
            had_space = false;
            at_line_start = true;
            lexer->advance(lexer, true);
        } else {
            break;
        }
    }

    // Hard wrap: a trailing " +" at the end of a line.
    if (valid_symbols[TOKEN_HARD_WRAP_PLUS] && had_space &&
        lexer->lookahead == '+') {
        lexer->advance(lexer, false);
        if (lexer->eof(lexer) || lexer->lookahead == '\n' ||
            lexer->lookahead == '\r') {
            lexer->markEnd(lexer);
            lexer->resultSymbol = TOKEN_HARD_WRAP_PLUS;
            return true;
        }
        return false;
    }

    int32_t la = lexer->lookahead;
    if (valid_symbols[TOKEN_EMPHASIS_BEGIN] && la == '*') {
        return scan_constrained_begin(lexer, '*', TOKEN_EMPHASIS_BEGIN);
    }
    if (valid_symbols[TOKEN_ITALIC_BEGIN] && la == '_') {
        return scan_constrained_begin(lexer, '_', TOKEN_ITALIC_BEGIN);
    }
    if (valid_symbols[TOKEN_MONOSPACE_BEGIN] && la == '`') {
        return scan_constrained_begin(lexer, '`', TOKEN_MONOSPACE_BEGIN);
    }
    if (valid_symbols[TOKEN_HIGHLIGHT_BEGIN] && la == '#') {
        return scan_constrained_begin(lexer, '#', TOKEN_HIGHLIGHT_BEGIN);
    }

    if (lexer->eof(lexer)) {
        if (valid_symbols[TOKEN_TYPE_EOF]) {
            lexer->resultSymbol = TOKEN_TYPE_EOF;
            return true;
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::asciidoc_inline
