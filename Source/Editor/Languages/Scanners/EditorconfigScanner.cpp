// The editorconfig external scanner, ported from https://github.com/ValdezFOmar/tree-sitter-editorconfig (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::editorconfig {

using namespace ned::editor::parse::scanner;

enum TokenType {
    END_OF_FILE,
    INTEGER_RANGE_START,
};

static inline bool is_digit(int32_t character) {
    return character >= '0' && character <= '9';
}

static inline bool parse_integer_range(Lexer *lexer) {
    int32_t previous = lexer->lookahead;
    lexer->advance(lexer, false);

    if (!is_digit(previous) && !(previous == '-' && is_digit(lexer->lookahead))) {
        return false;
    }

    while (is_digit(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
    // Integer ends here, but keep parsing to see if its a valid integer range
    lexer->markEnd(lexer);

    previous = lexer->lookahead;
    lexer->advance(lexer, false);

    if (!(previous == '.' && lexer->lookahead == '.')) {
        return false;
    }

    lexer->resultSymbol = INTEGER_RANGE_START;
    return true;
}

static bool Scan(
    void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
    if (valid_symbols[END_OF_FILE] && valid_symbols[INTEGER_RANGE_START]) {
        // Tree-sitter is in error correction mode, don't parse anything
        return false;
    }

    if (valid_symbols[END_OF_FILE] && lexer->eof(lexer)) {
        lexer->advance(lexer, false);
        lexer->markEnd(lexer);
        lexer->resultSymbol = END_OF_FILE;
        return true;
    }

    if (valid_symbols[INTEGER_RANGE_START]) {
        return parse_integer_range(lexer);
    }

    return false;
}

void *Create(void) {
    return NULL;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(
    void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::editorconfig
