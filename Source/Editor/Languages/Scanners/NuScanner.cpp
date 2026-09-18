// The nu external scanner, ported from https://github.com/nushell/tree-sitter-nu (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::nu {

using namespace ned::editor::parse::scanner;

#define skip lexer->advance(lexer, true)
#define adv lexer->advance(lexer, false)
#define eof lexer->eof(lexer)

enum TokenType {
    RAW_STRING_BEGIN,
    RAW_STRING_CONTENT,
    RAW_STRING_END,
    ERROR_SENTINEL,
};

typedef struct {
    uint8_t level;
} Scanner;

static uint8_t consume_chars(Lexer *lexer, char c) {
    uint8_t count = 0;
    while (lexer->lookahead == c && !eof) {
        adv;
        ++count;
    }
    return count;
}

static void skip_whitespace(Lexer *lexer) {
    while (iswspace(lexer->lookahead) && !eof) {
        skip;
    }
}

void *Create(void) {
    Scanner *scanner = scanner_malloc(sizeof(Scanner));
    scanner->level = 0;
    return scanner;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
    free(payload);
}

static unsigned Serialize(
    void *payload_,
    char *buffer
) {
    VoidPtr payload{payload_};
    Scanner *s = (Scanner *) payload;
    buffer[0] = s->level;
    return 1;
}

static void Deserialize(
    void *payload_,
    const char *buffer,
    unsigned length
) {
    VoidPtr payload{payload_};
    Scanner *s = (Scanner *) payload;
    s->level = 0;
    if (length == 1) {
        s->level = buffer[0];
    }
}

static bool scan_raw_string_begin(Lexer *lexer, Scanner *s) {
    // scan for r#' r##' or more #
    skip_whitespace(lexer);

    if (lexer->lookahead != 'r') {
        return false;
    }
    adv;
    uint8_t level = consume_chars(lexer, '#');
    if (lexer->lookahead == '\'') {
        adv;
        s->level = level;
        return true;
    }
    return false;
}

static bool scan_raw_string_content(Lexer *lexer, Scanner *s) {
    while (!eof) {
        lexer->markEnd(lexer);
        adv;
        uint8_t level = consume_chars(lexer, '#');
        if (level == s->level) {
            return true;
        }
    }

    return false;
}

static bool scan_raw_string_end(Lexer *lexer, Scanner *s) {
    // HINT: scan_raw_string_content already determines the content's length
    // so we only advance to the end of the delimiter and return true.
    adv;
    while (s->level > 0) {
        adv;
        s->level--;
    }
    return true;
}

static bool Scan(
    void *payload_,
    Lexer *lexer,
    const bool *valid_symbols
) {
    VoidPtr payload{payload_};
    if (valid_symbols[ERROR_SENTINEL]) {
        return false;
    }

    Scanner *s = (Scanner *) payload;

    if (valid_symbols[RAW_STRING_BEGIN] && s->level == 0) {
        lexer->resultSymbol = RAW_STRING_BEGIN;
        return scan_raw_string_begin(lexer, s);
    }
    if (valid_symbols[RAW_STRING_CONTENT] && s->level != 0) {
        lexer->resultSymbol = RAW_STRING_CONTENT;
        return scan_raw_string_content(lexer, s);
    }

    if (valid_symbols[RAW_STRING_END] && s->level != 0
            && lexer->lookahead == '\'') {
        lexer->resultSymbol = RAW_STRING_END;
        return scan_raw_string_end(lexer, s);
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::nu
