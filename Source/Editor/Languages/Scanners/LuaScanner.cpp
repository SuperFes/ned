// The lua external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-lua (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::lua {

using namespace ned::editor::parse::scanner;

enum TokenType {
    BLOCK_COMMENT_START,
    BLOCK_COMMENT_CONTENT,
    BLOCK_COMMENT_END,

    BLOCK_STRING_START,
    BLOCK_STRING_CONTENT,
    BLOCK_STRING_END,
};

static inline void consume(Lexer* lexer) {
    lexer->advance(lexer, false);
}

static inline void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}

static inline bool consume_char(char c, Lexer* lexer) {
    if (lexer->lookahead != c) {
        return false;
    }

    consume(lexer);
    return true;
}

static inline uint8_t consume_and_count_char(char c, Lexer* lexer) {
    uint8_t count = 0;
    while (lexer->lookahead == c) {
        ++count;
        consume(lexer);
    }
    return count;
}

static inline void skip_whitespaces(Lexer* lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
}

typedef struct {
    char    ending_char;
    uint8_t level_count;
} Scanner;

static inline void reset_state(Scanner* scanner) {
    scanner->ending_char = 0;
    scanner->level_count = 0;
}

void* Create() {
    Scanner* scanner = scanner_calloc(1, sizeof(Scanner));
    return scanner;
}

static void Destroy(void* payload_) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    free(scanner);
}

static unsigned Serialize(void* payload_, char* buffer) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    buffer[0]        = scanner->ending_char;
    buffer[1]        = (char)scanner->level_count;
    return 2;
}

static void Deserialize(void* payload_, const char* buffer, unsigned length) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;
    if (length == 0)
        return;
    scanner->ending_char = buffer[0];
    if (length == 1)
        return;
    scanner->level_count = buffer[1];
}

static bool scan_block_start(Scanner* scanner, Lexer* lexer) {
    if (consume_char('[', lexer)) {
        uint8_t level = consume_and_count_char('=', lexer);

        if (consume_char('[', lexer)) {
            scanner->level_count = level;
            return true;
        }
    }

    return false;
}

static bool scan_block_end(Scanner* scanner, Lexer* lexer) {
    if (consume_char(']', lexer)) {
        uint8_t level = consume_and_count_char('=', lexer);

        if (scanner->level_count == level && consume_char(']', lexer)) {
            return true;
        }
    }

    return false;
}

static bool scan_block_content(Scanner* scanner, Lexer* lexer) {
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == ']') {
            lexer->markEnd(lexer);

            if (scan_block_end(scanner, lexer)) {
                return true;
            }
        }
        else {
            consume(lexer);
        }
    }

    return false;
}

static bool scan_comment_start(Scanner* scanner, Lexer* lexer) {
    if (consume_char('-', lexer) && consume_char('-', lexer)) {
        lexer->markEnd(lexer);

        if (scan_block_start(scanner, lexer)) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = BLOCK_COMMENT_START;
            return true;
        }
    }

    return false;
}

static bool scan_comment_content(Scanner* scanner, Lexer* lexer) {
    if (scanner->ending_char == 0) { // block comment
        if (scan_block_content(scanner, lexer)) {
            lexer->resultSymbol = BLOCK_COMMENT_CONTENT;
            return true;
        }

        return false;
    }

    while (lexer->lookahead != 0) {
        if (lexer->lookahead == scanner->ending_char) {
            reset_state(scanner);
            lexer->resultSymbol = BLOCK_COMMENT_CONTENT;
            return true;
        }

        consume(lexer);
    }

    return false;
}

static bool Scan(void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr  payload{payload_};
    Scanner* scanner = (Scanner*)payload;

    if (valid_symbols[BLOCK_STRING_END] && scan_block_end(scanner, lexer)) {
        reset_state(scanner);
        lexer->resultSymbol = BLOCK_STRING_END;
        return true;
    }

    if (valid_symbols[BLOCK_STRING_CONTENT] && scan_block_content(scanner, lexer)) {
        lexer->resultSymbol = BLOCK_STRING_CONTENT;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT_END] && scanner->ending_char == 0 && scan_block_end(scanner, lexer)) {
        reset_state(scanner);
        lexer->resultSymbol = BLOCK_COMMENT_END;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT_CONTENT] && scan_comment_content(scanner, lexer)) {
        return true;
    }

    skip_whitespaces(lexer);

    if (valid_symbols[BLOCK_STRING_START] && scan_block_start(scanner, lexer)) {
        lexer->resultSymbol = BLOCK_STRING_START;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT_START]) {
        if (scan_comment_start(scanner, lexer)) {
            return true;
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::lua

NED_TREE_SITTER_SCANNER_EXPORTS(lua, ned::editor::languages::scanners::lua)
