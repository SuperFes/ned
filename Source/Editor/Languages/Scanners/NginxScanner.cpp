// The nginx external scanner, ported from https://github.com/opa-oz/tree-sitter-nginx (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::nginx {

using namespace ned::editor::parse::scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VEC_RESIZE(vec, _cap)                                                  \
    VoidPtr tmp{scanner_realloc((vec).data, (_cap) * sizeof((vec).data[0]))};  \
    assert(tmp);                                                               \
    (vec).data = tmp;                                                          \
    (vec).cap = (_cap);

#define VEC_GROW(vec, _cap)                                                    \
    if ((vec).cap < (_cap)) {                                                  \
        VEC_RESIZE((vec), (_cap));                                             \
    }

#define VEC_PUSH(vec, el)                                                      \
    if ((vec).cap == (vec).len) {                                              \
        VEC_RESIZE((vec), MAX(16, (vec).len * 2));                             \
    }                                                                          \
    (vec).data[(vec).len++] = (el);

#define VEC_POP(vec) (vec).len--;

#define VEC_NEW                                                                \
    { .len = 0, .cap = 0, .data = NULL }

#define VEC_BACK(vec) ((vec).data[(vec).len - 1])

#define VEC_FREE(vec)                                                          \
    {                                                                          \
        if ((vec).data != NULL)                                                \
            free((vec).data);                                                  \
    }

#define VEC_CLEAR(vec) (vec).len = 0;

enum TokenType { NEWLINE, INDENT, DEDENT };

typedef struct {
    uint32_t len;
    uint32_t cap;
    uint16_t *data;
} indent_vec;

static indent_vec indent_vec_new() {
    indent_vec vec = VEC_NEW;
    vec.data = scanner_calloc(1, sizeof(uint16_t));
    vec.cap = 1;
    return vec;
}

typedef struct {
    indent_vec indents;
} Scanner;

static inline void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static unsigned Serialize(void *payload_,
                                                    char *buffer) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    size_t size = 0;

    int iter = 1;
    for (; iter < scanner->indents.len &&
           size < kSerializationBufferSize;
         ++iter) {
        buffer[size++] = (char)scanner->indents.data[iter];
    }

    return size;
}

static void Deserialize(void *payload_,
                                                  const char *buffer,
                                                  unsigned length) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    VEC_CLEAR(scanner->indents);
    VEC_PUSH(scanner->indents, 0);

    if (length > 0) {
        for (size_t i = 0; i < length; i++) {
            VEC_PUSH(scanner->indents, (unsigned char)buffer[i]);
        }
        return;
    }
}

void *Create() {
    Scanner *scanner = scanner_calloc(1, sizeof(Scanner));
    scanner->indents = indent_vec_new();
    Deserialize(scanner, NULL, 0);
    return scanner;
}

static bool Scan(void *payload_, Lexer *lexer,
                                           const bool *valid_symbols) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    if (lexer->lookahead == '\n') {
        if (valid_symbols[NEWLINE]) {
            skip(lexer);
            lexer->resultSymbol = NEWLINE;
            return true;
        }
        return false;
    }

    if (lexer->lookahead && lexer->getColumn(lexer) == 0) {
        uint32_t indent_length = 0;

        // Indent tokens are zero width
        lexer->markEnd(lexer);

        for (;;) {
            if (lexer->lookahead == ' ') {
                indent_length++;
                skip(lexer);
            } else if (lexer->lookahead == '\t') {
                indent_length += 8;
                skip(lexer);
            } else {
                break;
            }
        }

        if (indent_length > VEC_BACK(scanner->indents) &&
            valid_symbols[INDENT]) {
            VEC_PUSH(scanner->indents, indent_length);
            lexer->resultSymbol = INDENT;
            return true;
        }
        if (indent_length < VEC_BACK(scanner->indents) &&
            valid_symbols[DEDENT]) {
            VEC_POP(scanner->indents);
            lexer->resultSymbol = DEDENT;
            return true;
        }
    }

    return false;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    VEC_FREE(scanner->indents);
    free(scanner);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::nginx

NED_SCANNER_LIBRARY_EXPORT(nginx, ned::editor::languages::scanners::nginx::kScanner)
