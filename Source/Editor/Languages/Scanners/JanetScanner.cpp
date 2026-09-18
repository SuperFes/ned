// The janet external scanner, ported from https://github.com/sogaiu/tree-sitter-janet-simple (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::janet {

using namespace ned::editor::parse::scanner;

enum TokenType {
    LONG_BUF_LIT,
    LONG_STR_LIT
};

static void* Create(
    void) {
    return NULL;
}

static void Destroy(
    void* payload_) {
    VoidPtr payload{payload_};
}

void tree_sitter_janet_simple_external_scanner_reset(
    void* payload) {
}

static unsigned Serialize(
    void* payload_,
    char* buffer) {
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(
    void*       payload_,
    const char* buffer,
    unsigned    length) {
    VoidPtr payload{payload_};
}

static bool Scan(
    void*       payload_,
    Lexer*      lexer,
    const bool* valid_symbols) {
    VoidPtr payload{payload_};
    // skip a bit brother
    while (iswspace(lexer->lookahead)) {
        lexer->advance(lexer, true);
    }
    // there can be only...two?
    if (valid_symbols[LONG_BUF_LIT] || valid_symbols[LONG_STR_LIT]) {
        // so which one was it?
        if (lexer->lookahead == '@') {
            lexer->resultSymbol = LONG_BUF_LIT;
            lexer->advance(lexer, false);
        }
        else {
            lexer->resultSymbol = LONG_STR_LIT;
        }
        // * long strings start with one or more backticks
        // * for a long buffer, the leading @ has been skipped (see above)
        //   to arrive at the first backtick
        // consume the first backtick
        if (lexer->lookahead != '`') {
            return false;
        }
        // getting here means a backtick was encountered
        lexer->advance(lexer, false);
        uint32_t n_backticks = 1;
        // arrive at a total number of backticks
        for (;;) {
            if (lexer->eof(lexer)) {
                return false;
            }
            // found one!
            if (lexer->lookahead == '`') {
                n_backticks++;
                lexer->advance(lexer, false);
                continue;
            }
            else { // nope, time to bail
                lexer->advance(lexer, false);
                break;
            }
        }
        // getting here means the last character examined was NOT a backtick.
        // now keep looking until n_backticks are found
        uint32_t cbt = 0; // consecutive backticks
        for (;;) {
            if (lexer->eof(lexer)) {
                return false;
            }
            // found one!
            if (lexer->lookahead == '`') {
                cbt++;
                // are we there yet?
                if (cbt == n_backticks) {
                    lexer->advance(lexer, false);
                    return true;
                }
            }
            else { // nope, better reset the count
                cbt = 0;
            }
            // next!
            lexer->advance(lexer, false);
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::janet
