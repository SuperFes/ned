// The cmake external scanner, ported from https://github.com/uyha/tree-sitter-cmake (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::cmake {

using namespace ned::editor::parse::scanner;

enum TokenType {
    BRACKET_ARGUMENT_OPEN,
    BRACKET_ARGUMENT_CONTENT,
    BRACKET_ARGUMENT_CLOSE,
    BRACKET_COMMENT_OPEN,
    BRACKET_COMMENT_CONTENT,
    BRACKET_COMMENT_CLOSE,
    LINE_COMMENT,
};

struct TreeSitterCMakeState {
    unsigned       level;
    enum TokenType token;
};

#define STATE_SIZE sizeof(struct TreeSitterCMakeState)

static bool eof(Lexer const* lexer) {
    return lexer->eof(lexer);
}
static void skip(Lexer* lexer) {
    lexer->advance(lexer, true);
}
static void advance(Lexer* lexer) {
    lexer->advance(lexer, false);
}
static void mark_end(Lexer* lexer) {
    lexer->markEnd(lexer);
}

static void skip_wspace(Lexer* lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
}

static bool is_open_brackets(struct TreeSitterCMakeState* state,
                             Lexer*                       lexer) {
    if (lexer->lookahead != '[') {
        return false;
    }

    advance(lexer);

    unsigned level = 0;
    while (lexer->lookahead == '=') {
        ++level;
        advance(lexer);
    }

    if (lexer->lookahead != '[') {
        return false;
    }

    advance(lexer);
    mark_end(lexer);

    state->level = level;
    return true;
}

static void parse_bracketed_content(struct TreeSitterCMakeState* state,
                                    Lexer*                       lexer) {
    while (!eof(lexer)) {
        if (lexer->lookahead == ']') {
            unsigned level = 0;

            mark_end(lexer);
            advance(lexer);
            while (lexer->lookahead == '=') {
                ++level;
                advance(lexer);
            }

            if (level == state->level && lexer->lookahead == ']') {
                break;
            }

            continue;
        }

        advance(lexer);
        mark_end(lexer);
    }
}

static bool is_close_brackets(struct TreeSitterCMakeState* state,
                              Lexer*                       lexer) {
    if (lexer->lookahead != ']') {
        return false;
    }

    unsigned level = 0;
    advance(lexer);
    while (lexer->lookahead == '=') {
        ++level;
        advance(lexer);
    }

    if (level != state->level || lexer->lookahead != ']') {
        return false;
    }

    advance(lexer);
    mark_end(lexer);

    state->level = 0;
    return true;
}

void* Create() {
    return scanner_malloc(sizeof(struct TreeSitterCMakeState));
}

static void Destroy(void* payload_) {
    VoidPtr payload{payload_};
    free(payload);
}

static unsigned Serialize(void* payload_,
                          char* buffer) {
    VoidPtr payload{payload_};
    memcpy(buffer, payload, STATE_SIZE);
    return STATE_SIZE;
}

static void Deserialize(void*       payload_,
                        char const* buffer,
                        unsigned    length) {
    VoidPtr payload{payload_};
    if (length == STATE_SIZE) {
        memcpy(payload, buffer, length);
    }
    else {
        struct TreeSitterCMakeState* state = payload;
        state->level                       = 0;
    }
}

static bool Scan(void* payload_, Lexer* lexer,
                 bool const* valid_symbols) {
    VoidPtr payload{payload_};

    struct TreeSitterCMakeState* state = payload;

    skip_wspace(lexer);

    if (valid_symbols[BRACKET_ARGUMENT_OPEN]) {
        if (is_open_brackets(payload, lexer)) {
            lexer->resultSymbol = BRACKET_ARGUMENT_OPEN;
            state->token        = BRACKET_ARGUMENT_OPEN;
            return true;
        }
    }
    if (valid_symbols[BRACKET_ARGUMENT_CONTENT] &&
        state->token == BRACKET_ARGUMENT_OPEN) {
        parse_bracketed_content(payload, lexer);
        lexer->resultSymbol = BRACKET_ARGUMENT_CONTENT;
        state->token        = BRACKET_ARGUMENT_CONTENT;
        return true;
    }
    if (valid_symbols[BRACKET_ARGUMENT_CLOSE] &&
        state->token == BRACKET_ARGUMENT_CONTENT) {
        if (is_close_brackets(payload, lexer)) {
            lexer->resultSymbol = BRACKET_ARGUMENT_CLOSE;
            return true;
        }
    }
    if (lexer->lookahead == '#') {
        if (!valid_symbols[BRACKET_COMMENT_OPEN] && !valid_symbols[LINE_COMMENT]) {
            return false;
        }

        advance(lexer);
        if (is_open_brackets(payload, lexer)) {
            lexer->resultSymbol = BRACKET_COMMENT_OPEN;
            state->token        = BRACKET_COMMENT_OPEN;
            return true;
        }

        while (lexer->lookahead != '\r' && lexer->lookahead != '\n' &&
               lexer->lookahead != '\0') {
            advance(lexer);
        }

        mark_end(lexer);
        lexer->resultSymbol = LINE_COMMENT;
        return true;
    }
    if (valid_symbols[BRACKET_COMMENT_CONTENT] &&
        state->token == BRACKET_COMMENT_OPEN) {
        parse_bracketed_content(payload, lexer);
        lexer->resultSymbol = BRACKET_COMMENT_CONTENT;
        state->token        = BRACKET_COMMENT_CONTENT;
        return true;
    }
    if (valid_symbols[BRACKET_COMMENT_CLOSE] &&
        state->token == BRACKET_COMMENT_CONTENT) {
        if (is_close_brackets(payload, lexer)) {
            lexer->resultSymbol = BRACKET_COMMENT_CLOSE;
            return true;
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::cmake
