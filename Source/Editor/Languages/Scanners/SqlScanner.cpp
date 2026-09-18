// The sql external scanner, ported from its upstream repository (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::sql {

using namespace ned::editor::parse::scanner;

enum TokenType {
    DOLLAR_QUOTED_STRING_START_TAG,
    DOLLAR_QUOTED_STRING_END_TAG,
    DOLLAR_QUOTED_STRING
};

#define MALLOC_STRING_SIZE 1024

typedef struct LexerState {
    char* start_tag;
} LexerState;

void* Create() {
    LexerState* state = scanner_malloc(sizeof(LexerState));
    state->start_tag  = NULL;
    return state;
}

static void Destroy(void* payload_) {
    VoidPtr     payload{payload_};
    LexerState* state = (LexerState*)payload;
    if (state->start_tag != NULL) {
        free(state->start_tag);
        state->start_tag = NULL;
    }
    free(payload);
}

static char* add_char(char* text, size_t* text_size, char c, int index) {
    if (text == NULL) {
        text       = scanner_malloc(sizeof(char) * MALLOC_STRING_SIZE);
        *text_size = MALLOC_STRING_SIZE;
    }

    // will break when indexes advances more than MALLOC_STRING_SIZE
    if (index + 1 >= *text_size) {
        *text_size += MALLOC_STRING_SIZE;
        char* tmp = scanner_malloc(*text_size * sizeof(char));
        strncpy(tmp, text, *text_size);
        free(text);
        text = tmp;
    }

    text[index]     = c;
    text[index + 1] = '\0';
    return text;
}

static char* scan_dollar_string_tag(Lexer* lexer) {
    char*   tag       = NULL;
    int     index     = 0;
    size_t* text_size = scanner_malloc(sizeof(size_t));
    *text_size        = 0;
    if (lexer->lookahead == '$') {
        tag = add_char(tag, text_size, '$', index);
        lexer->advance(lexer, false);
    }
    else {
        free(text_size);
        return NULL;
    }

    while (lexer->lookahead != '$' && !iswspace(lexer->lookahead) && !lexer->eof(lexer)) {
        tag = add_char(tag, text_size, lexer->lookahead, ++index);
        lexer->advance(lexer, false);
    }

    if (lexer->lookahead == '$') {
        tag = add_char(tag, text_size, lexer->lookahead, ++index);
        lexer->advance(lexer, false);
        free(text_size);
        return tag;
    }
    else {
        free(tag);
        free(text_size);
        return NULL;
    }
}

static bool Scan(void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr     payload{payload_};
    LexerState* state = (LexerState*)payload;
    if (valid_symbols[DOLLAR_QUOTED_STRING_START_TAG] && state->start_tag == NULL) {
        while (iswspace(lexer->lookahead))
            lexer->advance(lexer, true);

        char* start_tag = scan_dollar_string_tag(lexer);
        if (start_tag == NULL) {
            return false;
        }
        if (state->start_tag != NULL) {
            free(state->start_tag);
            state->start_tag = NULL;
        }
        state->start_tag    = start_tag;
        lexer->resultSymbol = DOLLAR_QUOTED_STRING_START_TAG;
        return true;
    }

    if (valid_symbols[DOLLAR_QUOTED_STRING_END_TAG] && state->start_tag != NULL) {
        while (iswspace(lexer->lookahead))
            lexer->advance(lexer, true);

        char* end_tag = scan_dollar_string_tag(lexer);
        if (end_tag != NULL && strcmp(end_tag, state->start_tag) == 0) {
            free(state->start_tag);
            state->start_tag    = NULL;
            lexer->resultSymbol = DOLLAR_QUOTED_STRING_END_TAG;
            free(end_tag);
            return true;
        }
        if (end_tag != NULL) {
            free(end_tag);
        }
        return false;
    }

    if (valid_symbols[DOLLAR_QUOTED_STRING]) {
        lexer->markEnd(lexer);
        while (iswspace(lexer->lookahead))
            lexer->advance(lexer, true);

        char* start_tag = scan_dollar_string_tag(lexer);
        if (start_tag == NULL) {
            return false;
        }

        // ned local fix (unreported upstream as of 2026-09-14): start_tag leaks
        // on this early return -- confirmed still present on upstream master,
        // caught by LeakSanitizer via Tests/ParseConformanceTest.cpp.
        if (state->start_tag != NULL && strcmp(state->start_tag, start_tag) == 0) {
            free(start_tag);
            return false;
        }

        char* end_tag = NULL;
        while (true) {
            if (lexer->eof(lexer)) {
                free(start_tag);
                free(end_tag);
                return false;
            }

            end_tag = scan_dollar_string_tag(lexer);
            if (end_tag == NULL) {
                lexer->advance(lexer, false);
                continue;
            }

            if (strcmp(end_tag, start_tag) == 0) {
                free(start_tag);
                free(end_tag);
                lexer->markEnd(lexer);
                lexer->resultSymbol = DOLLAR_QUOTED_STRING;
                return true;
            }

            free(end_tag);
            end_tag = NULL;
        }
    }

    return false;
}

static unsigned Serialize(void* payload_, char* buffer) {
    VoidPtr     payload{payload_};
    LexerState* state = (LexerState*)payload;
    if (state == NULL || state->start_tag == NULL) {
        return 0;
    }
    // + 1 for the '\0'
    int tag_length = strlen(state->start_tag) + 1;
    if (tag_length >= kSerializationBufferSize) {
        return 0;
    }

    memcpy(buffer, state->start_tag, tag_length);
    if (state->start_tag != NULL) {
        free(state->start_tag);
        state->start_tag = NULL;
    }
    return tag_length;
}

static void Deserialize(void* payload_, const char* buffer, unsigned length) {
    VoidPtr     payload{payload_};
    LexerState* state = (LexerState*)payload;
    // ned local fix (unreported upstream as of 2026-09-14): overwriting
    // start_tag here without freeing it first leaks whatever a prior
    // scan()/deserialize() call left allocated -- deserialize runs on every
    // lex attempt while in this scanner's lex state, so a real parse leaks
    // once per such attempt. Confirmed still present on upstream master.
    if (state->start_tag != NULL) {
        free(state->start_tag);
    }
    state->start_tag = NULL;
    // A length of 1 can't exists.
    if (length > 1) {
        state->start_tag = scanner_malloc(length);
        memcpy(state->start_tag, buffer, length);
    }
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::sql
