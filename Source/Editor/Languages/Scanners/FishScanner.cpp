// The fish external scanner, ported from https://github.com/ram02z/tree-sitter-fish (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::fish {

using namespace ned::editor::parse::scanner;

enum TokenType {
    CONCAT,
    BRACKET_CONCAT,
    CONCAT_LIST,
    BEGIN_BRACE,
};

void* Create() {
    return NULL;
}
static void Destroy(void* p_) {
    VoidPtr p{p_};
}
void tree_sitter_fish_external_scanner_reset(void* p) {
}
static unsigned Serialize(void* p_, char* buffer) {
    VoidPtr p{p_};
    return 0;
}
static void Deserialize(void* p_, const char* b, unsigned n) {
    VoidPtr p{p_};
}

static bool Scan(
    void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr payload{payload_};
    // BEGIN_BRACE: { followed by whitespace or ; (for begin_statement)
    // Must take priority over internal '{' token used by brace_expansion
    if (valid_symbols[BEGIN_BRACE]) {
        // Skip leading whitespace (since whitespace is in extras)
        while (iswspace(lexer->lookahead)) {
            lexer->advance(lexer, true); // skip=true for whitespace
        }

        if (lexer->lookahead == '{') {
            lexer->advance(lexer, false); // consume '{'
            if (lexer->lookahead == ';' || iswspace(lexer->lookahead)) {
                lexer->markEnd(lexer);
                lexer->resultSymbol = BEGIN_BRACE;
                return true;
            }
        }
        // Not matched - return false to let internal lexer try
        // (returning false resets lexer state)
    }

    if (valid_symbols[CONCAT_LIST]) {
        if (!(
                lexer->lookahead == 0 ||
                lexer->lookahead != '[')) {
            lexer->resultSymbol = CONCAT_LIST;
            return true;
        }
    }

    if (valid_symbols[CONCAT]) {
        if (!(
                lexer->lookahead == 0 ||
                lexer->lookahead == '>' ||
                lexer->lookahead == '<' ||
                lexer->lookahead == ')' ||
                lexer->lookahead == ';' ||
                lexer->lookahead == '&' ||
                lexer->lookahead == '|' ||
                iswspace(lexer->lookahead))) {
            lexer->resultSymbol = CONCAT;
            return true;
        }
    }

    if (valid_symbols[BRACKET_CONCAT]) {
        if (!(
                lexer->lookahead == 0 ||
                lexer->lookahead == ')' ||
                lexer->lookahead == '(' ||
                lexer->lookahead == '}' ||
                lexer->lookahead == ',' ||
                iswspace(lexer->lookahead))) {
            lexer->resultSymbol = BRACKET_CONCAT;
            return true;
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::fish
