// The gitcommit external scanner, ported from https://github.com/gbprod/tree-sitter-gitcommit (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::gitcommit {

using namespace ned::editor::parse::scanner;

enum TokenType { CONVENTIONAL_PREFIX,
                 TRAILER_VALUE };

void* Create() {
    return NULL;
}

static void Destroy(void* p_) {
    VoidPtr p{p_};
}

void tree_sitter_gitcommit_external_scanner_reset(void* p) {
}

static unsigned Serialize(void* p_,
                          char* buffer) {
    VoidPtr p{p_};
    return 0;
}

static void Deserialize(void* p_, const char* b,
                        unsigned n) {
    VoidPtr p{p_};
}

static bool Scan(void* payload_, Lexer* lexer,
                 const bool* valid_symbols) {
    VoidPtr payload{payload_};
    if (valid_symbols[CONVENTIONAL_PREFIX]) {
        lexer->resultSymbol = CONVENTIONAL_PREFIX;
        if (iswcntrl(lexer->lookahead) || iswspace(lexer->lookahead) ||
            lexer->lookahead == ':' || lexer->lookahead == '!' ||
            lexer->lookahead == '\0') {
            return false;
        }
        lexer->advance(lexer, false);

        while (!iswcntrl(lexer->lookahead) && !iswspace(lexer->lookahead) &&
               lexer->lookahead != ':' && lexer->lookahead != '!' &&
               lexer->lookahead != '(' && lexer->lookahead != ')' &&
               lexer->lookahead != '\0') {
            lexer->advance(lexer, false);
        }
        lexer->markEnd(lexer);

        if (lexer->lookahead == '(') {
            lexer->advance(lexer, false);

            if (lexer->lookahead == ')') {
                return false;
            }

            while (!iswcntrl(lexer->lookahead) && lexer->lookahead != '(' &&
                   lexer->lookahead != ')' && lexer->lookahead != '\0') {
                lexer->advance(lexer, false);
            }

            if (lexer->lookahead != ')') {
                return false;
            }
            lexer->advance(lexer, false);
        }

        if (lexer->lookahead == '!') {
            lexer->advance(lexer, false);
        }

        return lexer->lookahead == ':' || lexer->lookahead == 0xff1a;
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::gitcommit
