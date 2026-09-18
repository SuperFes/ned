// The css external scanner, ported from https://github.com/tree-sitter/tree-sitter-css (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::css {

using namespace ned::editor::parse::scanner;

enum TokenType {
    DESCENDANT_OP,
    PSEUDO_CLASS_SELECTOR_COLON,
    ERROR_RECOVERY,
};

static inline void advance(Lexer* lexer) {
    lexer->advance(lexer, false);
}

static inline void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}

void* Create() {
    return NULL;
}

static void Destroy(void* payload_) {
    VoidPtr payload{payload_};
}

static unsigned Serialize(void* payload_, char* buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(void* payload_, const char* buffer, unsigned length) {
    VoidPtr payload{payload_};
}

static bool Scan(void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr payload{payload_};
    if (valid_symbols[ERROR_RECOVERY]) {
        return false;
    }

    if (iswspace(lexer->lookahead) && valid_symbols[DESCENDANT_OP]) {
        lexer->resultSymbol = DESCENDANT_OP;

        skip(lexer);
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        lexer->markEnd(lexer);

        if (lexer->lookahead == '#' || lexer->lookahead == '.' || lexer->lookahead == '[' || lexer->lookahead == '-' ||
            lexer->lookahead == '*' || iswalnum(lexer->lookahead)) {
            return true;
        }

        if (lexer->lookahead == ':') {
            advance(lexer);
            if (iswspace(lexer->lookahead)) {
                return false;
            }
            for (;;) {
                if (lexer->lookahead == ';' || lexer->lookahead == '}' || lexer->eof(lexer)) {
                    return false;
                }
                if (lexer->lookahead == '{') {
                    return true;
                }
                advance(lexer);
            }
        }
    }

    if (valid_symbols[PSEUDO_CLASS_SELECTOR_COLON]) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        if (lexer->lookahead == ':') {
            advance(lexer);
            if (lexer->lookahead == ':') {
                return false;
            }
            lexer->markEnd(lexer);
            lexer->resultSymbol = PSEUDO_CLASS_SELECTOR_COLON;

            // We need a `{` to be a pseudo class selector, a `;` indicates a property.
            // This does not apply if we're in a comment, however.
            bool in_comment = false;
            while (lexer->lookahead != ';' && lexer->lookahead != '}' && !lexer->eof(lexer)) {
                advance(lexer);
                if (lexer->lookahead == '{' && !in_comment) {
                    return true;
                }
                if (lexer->lookahead == '/' && !in_comment) {
                    advance(lexer);
                    if (lexer->lookahead == '*') {
                        in_comment = true;
                    }
                }
                else if (lexer->lookahead == '*' && in_comment) {
                    advance(lexer);
                    if (lexer->lookahead == '/') {
                        in_comment = false;
                    }
                }
            }

            // If we're at eof, and we happened to *not* find an opening brace to indicate we have a pseudo class
            // selector, we should *still* return one at EOF. This will improve error recovery, and the malformed code
            // can be parsed as an erroneous pseudo-class selector, rather than an erroneous property.
            return lexer->eof(lexer);
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::css

NED_TREE_SITTER_SCANNER_EXPORTS(css, ned::editor::languages::scanners::css)
