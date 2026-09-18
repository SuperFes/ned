// The dotenv external scanner, ported from https://github.com/pnx/tree-sitter-dotenv (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::dotenv {

using namespace ned::editor::parse::scanner;

enum TokenType {
    END_OF_ASSIGNMENT,
};

void *Create(void) {
    return NULL;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};}

void static advanceWS(Lexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
    }
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};

    if (valid_symbols[END_OF_ASSIGNMENT]) {
        advanceWS(lexer);

        if (lexer->lookahead == '\r') {
            lexer->advance(lexer, true);
        }

        if (lexer->eof(lexer) 
            || lexer->lookahead == '#'
            || lexer->lookahead == '\n') {
            lexer->resultSymbol = END_OF_ASSIGNMENT;
            return true;
        }
    }
    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::dotenv
