// The julia external scanner, ported from https://github.com/tree-sitter/tree-sitter-julia (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::julia {

using namespace ned::editor::parse::scanner;

/// Block comments and immediate parentheses are easy to parse, but strings
/// require extra-attention.
///
/// The main problems that arise when parsing strings are:
/// 1. Triple quoted strings allow single quotes inside. e.g. """ "foo" """.
/// 2. Strings can have arbitrary interpolations, including other strings.
///    e.g. "echo $("foo")"
/// 3. Non-standard string literals don't allow interpolations or escape
///    sequences, but you can always write \" and \`.
/// All of the above also applies to command literals.
enum TokenType {
    BLOCK_COMMENT_REST,
    IMMEDIATE_PAREN,
    IMMEDIATE_BRACKET,
    IMMEDIATE_BRACE,
    IMMEDIATE_STRING_START,
    IMMEDIATE_COMMAND_START,
    CONTENT_CMD_1,
    CONTENT_CMD_1_RAW,
    CONTENT_CMD_3,
    CONTENT_CMD_3_RAW,
    CONTENT_STR_1,
    CONTENT_STR_1_RAW,
    CONTENT_STR_3,
    CONTENT_STR_3_RAW,
    END_CMD,
    END_STR,
};

void *Create() {
    return NULL;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_}; return 0; }

static void Deserialize(void *payload_, const char *buffer, unsigned size) {
    VoidPtr payload{payload_};}

// Scanner functions

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static void mark_end(Lexer *lexer) { lexer->markEnd(lexer); }

static bool scan_content(Lexer *lexer, Symbol content_symbol, char end_char, unsigned n_delim, bool interp) {
    Symbol end_symbol = (end_char == '"') ? END_STR : END_CMD;
    bool has_content = false;
    int32_t next;
    while ((next = lexer->lookahead)) {
        mark_end(lexer);
        if (interp && (next == '$' || next == '\\')) {
            lexer->resultSymbol = content_symbol;
            return has_content;
        } else if (next == '\\') {
            // Parse backslash in raw strings (check escaped delimiters and '\\')
            advance(lexer);
            next = lexer->lookahead;
            if (next == end_char || next == '\\') {
                lexer->resultSymbol = content_symbol;
                return has_content;
            }
        } else {
            bool is_end_delimiter = true;
            for (unsigned i = 1; i <= n_delim; i++) {
                if (lexer->lookahead == end_char) {
                    advance(lexer);
                } else {
                    is_end_delimiter = false;
                    break;
                }
            }
            if (is_end_delimiter) {
                if (has_content) {
                    lexer->resultSymbol = content_symbol;
                    return true;
                } else {
                    mark_end(lexer);
                    lexer->resultSymbol = end_symbol;
                    return true;
                }
            }
        }
        advance(lexer);
        has_content = true;
    }
    return false;
}

static bool scan_block_comment(Lexer *lexer) {
    // NOTE: The first `#=` is scanned by tree-sitter
    bool after_eq = false;
    unsigned nesting_depth = 1;
    for (;;) {
        switch (lexer->lookahead) {
            case '=':
                advance(lexer);
                after_eq = true;
                break;
            case '#':
                advance(lexer);
                if (after_eq) {
                    after_eq = false;
                    nesting_depth--;
                    if (nesting_depth == 0) {
                        lexer->resultSymbol = BLOCK_COMMENT_REST;
                        return true;
                    }
                } else {
                    after_eq = false;
                    if (lexer->lookahead == '=') {
                        advance(lexer);
                        nesting_depth++;
                    }
                }
                break;
            case '\0':
                return false;
            default:
                advance(lexer);
                after_eq = false;
                break;
        }
    }
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
    if (valid_symbols[IMMEDIATE_PAREN] && lexer->lookahead == '(') {
        lexer->resultSymbol = IMMEDIATE_PAREN;
        return true;
    } else if (valid_symbols[IMMEDIATE_BRACKET] && lexer->lookahead == '[') {
        lexer->resultSymbol = IMMEDIATE_BRACKET;
        return true;
    } else if (valid_symbols[IMMEDIATE_BRACE] && lexer->lookahead == '{') {
        lexer->resultSymbol = IMMEDIATE_BRACE;
        return true;
    } else if (valid_symbols[IMMEDIATE_STRING_START] && lexer->lookahead == '"') {
        lexer->resultSymbol = IMMEDIATE_STRING_START;
        return true;
    } else if (valid_symbols[IMMEDIATE_COMMAND_START] && lexer->lookahead == '`') {
        lexer->resultSymbol = IMMEDIATE_COMMAND_START;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT_REST] && scan_block_comment(lexer)) {
        return true;
    }

    if (valid_symbols[CONTENT_STR_1] && scan_content(lexer, CONTENT_STR_1, '"', 1, true)) {
        return true;
    }

    if (valid_symbols[CONTENT_STR_3] && scan_content(lexer, CONTENT_STR_3, '"', 3, true)) {
        return true;
    }

    if (valid_symbols[CONTENT_CMD_1] && scan_content(lexer, CONTENT_CMD_1, '`', 1, true)) {
        return true;
    }

    if (valid_symbols[CONTENT_CMD_3] && scan_content(lexer, CONTENT_CMD_3, '`', 3, true)) {
        return true;
    }

    if (valid_symbols[CONTENT_STR_1_RAW] && scan_content(lexer, CONTENT_STR_1_RAW, '"', 1, false)) {
        return true;
    }

    if (valid_symbols[CONTENT_STR_3_RAW] && scan_content(lexer, CONTENT_STR_3_RAW, '"', 3, false)) {
        return true;
    }

    if (valid_symbols[CONTENT_CMD_1_RAW] && scan_content(lexer, CONTENT_CMD_1_RAW, '`', 1, false)) {
        return true;
    }

    if (valid_symbols[CONTENT_CMD_3_RAW] && scan_content(lexer, CONTENT_CMD_3_RAW, '`', 3, false)) {
        return true;
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::julia
