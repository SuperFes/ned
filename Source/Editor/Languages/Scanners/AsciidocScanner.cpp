// The asciidoc external scanner, ported from https://github.com/cathaysia/tree-sitter-asciidoc (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::asciidoc {

using namespace ned::editor::parse::scanner;

// --- scanner.h (from the same grammar) ---

// --- base_types.h (from the same grammar) ---

#ifndef u8
typedef uint8_t u8;
#endif

#ifndef i8
typedef int8_t i8;
#endif

#ifndef u32
typedef uint32_t u32;
#endif

#ifndef i32
typedef int32_t i32;
#endif

#ifndef usize
typedef uintptr_t usize;
#endif

#ifndef isize
typedef intptr_t isize;
#endif

#ifndef USIZE_MAX
static const usize USIZE_MAX = UINTPTR_MAX;
#endif

#ifndef ISIZE_MAX
static const isize ISIZE_MAX = INTPTR_MAX;
#endif

#define ADOC_UNUSED(x) ((void)x)

// --- quick_buffer.h (from the same grammar) ---
// --- base_types.h already inlined above ---

#ifndef bzero
#define bzero(dst, len) memset(dst, 0, len)
#endif  // !bzero

typedef enum Result {
    RESULT_ERR = 0,
    RESULT_OK = 1,
} Result;

typedef struct QuickBuffer {
    void* buffer;
    usize pos;
    usize capacity;
} QuickBuffer;

static QuickBuffer quick_buffer_new(void* buffer, usize capacity) {
    QuickBuffer b;
    b.buffer = buffer;
    b.pos = 0;
    b.capacity = capacity;

    return b;
}

#define impl_write_for(ty)                                                      \
    static inline Result quick_buffer_write_##ty(QuickBuffer* self, ty value) { \
        if(self->capacity - self->pos < sizeof(ty)) {                           \
            return RESULT_ERR;                                                  \
        }                                                                       \
                                                                                \
        memcpy((u8*)self->buffer + self->pos, &value, sizeof(ty));              \
        self->pos += sizeof(ty);                                                \
                                                                                \
        return RESULT_OK;                                                       \
    }

impl_write_for(bool);
impl_write_for(u8);
impl_write_for(i8);
impl_write_for(u32);
impl_write_for(usize);

#define impl_read_for(ty)                                                       \
    static inline Result quick_buffer_read_##ty(QuickBuffer* self, ty* value) { \
        if(self->capacity - self->pos < sizeof(ty)) {                           \
            return RESULT_ERR;                                                  \
        }                                                                       \
                                                                                \
        memcpy(value, (u8*)self->buffer + self->pos, sizeof(ty));               \
        self->pos += sizeof(ty);                                                \
                                                                                \
        return RESULT_OK;                                                       \
    }

impl_read_for(bool);
impl_read_for(u8);
impl_read_for(i8);
impl_read_for(u32);
impl_read_for(usize);

static inline Result quick_buffer_extend_bytes(QuickBuffer* self, void const* buffer, usize len) {
    if(self->capacity - self->pos < len) {
        return RESULT_ERR;
    }

    memcpy((u8*)self->buffer + self->pos, buffer, len);
    self->pos += len;

    return RESULT_OK;
}

static inline Result quick_buffer_read_bytes(QuickBuffer* self, void* value, usize len) {
    if(self->capacity - self->pos < len) {
        return RESULT_ERR;
    }

    memcpy(value, (u8*)self->buffer + self->pos, len);
    self->pos += len;

    return RESULT_OK;
}

typedef enum BlockKind {
    BLOCK_KIND_DELIMITED,
    BLOCK_KIND_TABLE,
    BLOCK_KIND_LISTING,
    BLOCK_KIND_LITERAL,
    BLOCK_KIND_SIDEBAR,
    BLOCK_KIND_QUOTED,
    BLOCK_KIND_CSV_TABLE,
    BLOCK_KIND_DSV_TABLE
} BlockKind;

typedef struct Node {
    BlockKind kind;
    usize counter;
} Node;

typedef struct Scanner {
    usize capacity;
    usize len;
    Node *buffer;
} Scanner;

static inline void scanner_init(Scanner *self);
static inline void scanner_free(Scanner *self);

static inline Node *scanner_top(Scanner const *self);
static inline bool scanner_is_matching_raw_block(Scanner *self);
static inline bool scanner_is_expect_block_start(Scanner const *self);
/**
 * @brief check scanner is matching a block
 *
 * @param self Scanner
 * @param kind BlockKind
 * @param counter block counter, zero means any counter.
 * @return true if matched.
 */
static inline bool scanner_is_matching(Scanner const *self, BlockKind kind, usize counter);
static inline Result scanner_serialize(Scanner const *self, QuickBuffer *qb);
static inline Result scanner_deserialize(Scanner *self, QuickBuffer *qb);
/**
 * @brief pop if scanner_is_matching block
 *
 * @param self Scanner
 * @param kind BlockKind
 * @param counter block counter, zero means any counter.
 * @return true if pop a block.
 */
static inline bool scanner_pop_kind(Scanner *self, BlockKind kind, usize counter);
static inline void scanner_pop(Scanner *self);
static inline void scanner_push(Scanner *self, BlockKind kind, usize counter);

// --- base_types.h already inlined above ---
// --- quick_buffer.h already inlined above ---
// --- utils.h (from the same grammar) ---

// --- base_types.h already inlined above ---

typedef enum TokenType {
    TOKEN_TYPE_EOF,
    TOKEN_TITLE_H0_MARKER,
    TOKEN_TITLE_H1_MARKER,
    TOKEN_TITLE_H2_MARKER,
    TOKEN_TITLE_H3_MARKER,
    TOKEN_TITLE_H4_MARKER,
    TOKEN_TITLE_H5_MARKER,
    TOKEN_LIST_MARKER_STAR,
    TOKEN_LIST_MARKER_HYPHEN,
    TOKEN_LIST_MARKER_DOT,
    TOKEN_LIST_MARKER_DIGIT,
    TOKEN_LIST_MARKER_GEEK,
    TOKEN_LIST_MARKER_ALPHA,
    TOKEN_DOCUMENT_ATTR_MARKER,
    TOKEN_ELEMENT_ATTR_MARKER,
    TOKEN_BLOCK_TITLE_MARKER,
    TOKEN_BREAKS_MARKS,
    TOKEN_TABLE_BLOCK_MARKER,
    TOKEN_NTABLE_BLOCK_MARKER,
    TOKEN_CELL_ATTR,
    TOKEN_DELIMITED_BLOCK_START_MARKER,
    TOKEN_DELIMITED_BLOCK_END_MARKER,
    TOKEN_LISTING_BLOCK_START_MARKER,
    TOKEN_LISTING_BLOCK_END_MARKER,
    TOKEN_LITERAL_BLOCK_MARKER,
    TOKEN_QUOTED_BLOCK_START_MARKER,
    TOKEN_QUOTED_BLOCK_END_MARKER,
    TOKEN_QUOTED_BLOCK_MD_MARKER,
    TOKEN_QUOTED_PARAGRAPH_MARKER,
    TOKEN_OPEN_BLOCK_MARKER,
    TOKEN_PASSTHROUGH_BLOCK_MARKER,
    TOKEN_BLOCK_MACRO_NAME,
    TOKEN_CALLOUT_MARKER,
    TOKEN_CALLOUT_LIST_MARKER,
    TOKEN_LINE_COMMENT_MARKER,
    TOKEN_BLOCK_COMMENT_START_MARKER,
    TOKEN_BLOCK_COMMENT_END_MARKER,

    TOKEN_ADMONITION_NOTE,
    TOKEN_ADMONITION_TIP,
    TOKEN_ADMONITION_IMPORTANT,
    TOKEN_ADMONITION_CAUTION,
    TOKEN_ADMONITION_WARNING,
    TOKEN_IDENT_MARKER,
    TOKEN_LIST_CONTINUATION,
    TOKEN_SIDEBAR_BLOCK_START_MARKER,
    TOKEN_SIDEBAR_BLOCK_END_MARKER,
    TOKEN_CSV_TABLE_BLOCK_MARKER,
    TOKEN_DSV_TABLE_BLOCK_MARKER,
    TOKEN_TERM,
    TOKEN_DESCRIPTION_MARKER
} TokenType;

static inline bool parse_table_attr(Lexer *lexer);
static inline bool parse_number(Lexer *lexer);
static inline bool parse_sequence_impl(Lexer *lexer, char const *sequence, usize len);
#define parse_sequence(lexer, sequence) parse_sequence_impl(lexer, sequence, sizeof(sequence) - 1)
static inline bool parse_ordered_marker(Lexer *lexer);
static inline bool parse_breaks(char start, Lexer *lexer);
static inline bool consume(i32 ch, Lexer *lexer, bool skip_space, usize *counter, usize max);
static inline bool skip_white_space(Lexer *lexer);
static inline bool is_white_space(i32 ch);
static inline bool is_new_line(i32 ch);
static inline bool is_ascii_digit(i32 ch);
static inline bool is_ascii_alpha_lower(i32 ch);
static inline bool is_geek_lower(i32 ch);
static inline bool is_newline(i32 ch);
static inline bool is_eof(Lexer *lexer);

#ifndef LIST_INIT_SIZE
#define LIST_INIT_SIZE 10
#endif  // ifndef LIST_INIT_SIZE

#ifndef LIST_GROW_SIZE
#define LIST_GROW_SIZE 10
#endif  // ifndef LIST_GROW_SIZE

void *Create() {
    Scanner *scanner = (Scanner *)scanner_malloc(sizeof(Scanner));
    scanner_init(scanner);
    return scanner;
}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;
    scanner_free(scanner);
    free(scanner);
}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
    Scanner *scanner = (Scanner *)payload;

    QuickBuffer qb = quick_buffer_new(buffer, kSerializationBufferSize);

    scanner_serialize(scanner, &qb);

    return qb.pos;
}

static void Deserialize(void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};
    if(!buffer) {
        return;
    }

    Scanner *s = (Scanner *)payload;
    QuickBuffer qb = quick_buffer_new((void *)buffer, length);
    scanner_deserialize(s, &qb);
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
    Scanner *s = (Scanner *)payload;

    if(lexer->eof(lexer)) {
        if(valid_symbols[TOKEN_TYPE_EOF]) {
            lexer->resultSymbol = TOKEN_TYPE_EOF;
            return true;
        }
    }

    usize start_pos = lexer->getColumn(lexer);

    if(valid_symbols[TOKEN_BLOCK_COMMENT_END_MARKER]) {
        if(start_pos != 0) {
            return false;
        }
        if(parse_sequence(lexer, "////")) {
            if(is_newline(lexer->lookahead)) {
                lexer->resultSymbol = TOKEN_BLOCK_COMMENT_END_MARKER;
                return true;
            }
        }

        return false;
    }

    if(start_pos == 0) {
        if(valid_symbols[TOKEN_ADMONITION_NOTE]) {
            do {
                if(parse_sequence(lexer, "NOTE")) {
                    lexer->resultSymbol = TOKEN_ADMONITION_NOTE;
                } else if(parse_sequence(lexer, "TIP")) {
                    lexer->resultSymbol = TOKEN_ADMONITION_TIP;
                } else if(parse_sequence(lexer, "IMPORTANT")) {
                    lexer->resultSymbol = TOKEN_ADMONITION_IMPORTANT;
                } else if(parse_sequence(lexer, "CAUTION")) {
                    lexer->resultSymbol = TOKEN_ADMONITION_CAUTION;
                } else if(parse_sequence(lexer, "WARNING")) {
                    lexer->resultSymbol = TOKEN_ADMONITION_WARNING;
                } else {
                    break;
                }

                lexer->markEnd(lexer);
                if(lexer->lookahead == ':') {
                    lexer->advance(lexer, true);
                    if(lexer->lookahead == ' ') {
                        return true;
                    }
                }
            } while(0);
        }

        bool is_alpha_lower = is_ascii_alpha_lower(lexer->lookahead);
        if(valid_symbols[TOKEN_LIST_MARKER_ALPHA]) {
            if(parse_ordered_marker(lexer)) {
                return true;
            }
        }

        if(is_alpha_lower && scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME]) {
                if(parse_sequence(lexer, "include")) {
                    lexer->markEnd(lexer);
                    if(parse_sequence(lexer, "::")) {
                        lexer->resultSymbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        if(is_alpha_lower && !scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] || valid_symbols[TOKEN_TERM]) {
                while(is_ascii_alpha_lower(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                lexer->markEnd(lexer);
                i32 c = lexer->lookahead;
                if(c == ':' || c == ';') {
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool followed_ws = is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer);
                    bool desc_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    // `name:: ` (delimiter followed by space/EOL) is a
                    // description-list term, whereas `name::target[]` is a
                    // block macro.markEnd sits after the bare word, so the
                    // delimiter run we just consumed is only lookahead.
                    if(valid_symbols[TOKEN_TERM] && desc_run && followed_ws) {
                        lexer->resultSymbol = TOKEN_TERM;
                        return true;
                    }
                    if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] && c == ':' && run >= 2) {
                        lexer->resultSymbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        if(lexer->getColumn(lexer) == 0) {
            switch(lexer->lookahead) {
                case '=': {
                    consume('=', lexer, false, NULL, USIZE_MAX);
                    lexer->markEnd(lexer);
                    if(!scanner_is_matching_raw_block(s)) {
                        usize counter = lexer->getColumn(lexer);
                        if(counter >= 4 && is_new_line(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DELIMITED, counter)) {
                                lexer->resultSymbol = TOKEN_DELIMITED_BLOCK_END_MARKER;
                                scanner_pop(s);
                            } else {
                                scanner_push(s, BLOCK_KIND_DELIMITED, counter);
                                lexer->resultSymbol = TOKEN_DELIMITED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }

                    if(!scanner_is_matching_raw_block(s)) {
                        usize level = TOKEN_TITLE_H0_MARKER - 1 + lexer->getColumn(lexer);
                        if(level <= TOKEN_TITLE_H5_MARKER && is_white_space(lexer->lookahead)) {
                            lexer->resultSymbol = level;
                            return true;
                        }
                    }
                    break;
                }
                case '*': {
                    usize counter = 0;
                    consume('*', lexer, false, &counter, USIZE_MAX);
                    lexer->markEnd(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);

                    if(valid_symbols[TOKEN_SIDEBAR_BLOCK_START_MARKER] || valid_symbols[TOKEN_SIDEBAR_BLOCK_END_MARKER]) {
                        usize col = lexer->getColumn(lexer);
                        if(col >= 4 && (is_new_line(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_SIDEBAR, col)) {
                                scanner_pop(s);
                                lexer->resultSymbol = TOKEN_SIDEBAR_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_SIDEBAR, col);
                                    lexer->resultSymbol = TOKEN_SIDEBAR_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '*') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->resultSymbol = TOKEN_BREAKS_MARKS;
                            lexer->markEnd(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_STAR]) {
                        lexer->resultSymbol = TOKEN_LIST_MARKER_STAR;
                        return is_unordered_marker;
                    }

                    break;
                }
                case '-': {
                    usize counter = 0;
                    consume('-', lexer, false, &counter, USIZE_MAX);
                    lexer->markEnd(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);
                    if(valid_symbols[TOKEN_OPEN_BLOCK_MARKER]) {
                        if(lexer->getColumn(lexer) == 2 && is_newline(lexer->lookahead)) {
                            lexer->resultSymbol = TOKEN_OPEN_BLOCK_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_QUOTED_PARAGRAPH_MARKER]) {
                        if(lexer->getColumn(lexer) == 2 && is_white_space(lexer->lookahead)) {
                            lexer->resultSymbol = TOKEN_QUOTED_PARAGRAPH_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LISTING_BLOCK_START_MARKER] || valid_symbols[TOKEN_LISTING_BLOCK_END_MARKER]) {
                        usize counter = lexer->getColumn(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LISTING, counter)) {
                                scanner_pop(s);
                                lexer->resultSymbol = TOKEN_LISTING_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_LISTING, counter);
                                    lexer->resultSymbol = TOKEN_LISTING_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '-') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->resultSymbol = TOKEN_BREAKS_MARKS;
                            lexer->markEnd(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_HYPHEN] && is_unordered_marker) {
                        lexer->resultSymbol = TOKEN_LIST_MARKER_HYPHEN;
                        return true;
                    }
                    break;
                }
                case '.': {
                    consume('.', lexer, false, NULL, USIZE_MAX);
                    lexer->markEnd(lexer);
                    if(valid_symbols[TOKEN_LITERAL_BLOCK_MARKER]) {
                        usize counter = lexer->getColumn(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LITERAL, counter)) {
                                scanner_pop(s);
                                lexer->resultSymbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            } else if(!scanner_is_matching_raw_block(s)) {
                                scanner_push(s, BLOCK_KIND_LITERAL, counter);
                                lexer->resultSymbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_DOT]) {
                        if(is_white_space(lexer->lookahead)) {
                            lexer->markEnd(lexer);
                            lexer->resultSymbol = TOKEN_LIST_MARKER_DOT;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_BLOCK_TITLE_MARKER]) {
                        if(lexer->getColumn(lexer) == 1) {
                            lexer->markEnd(lexer);
                            lexer->resultSymbol = TOKEN_BLOCK_TITLE_MARKER;
                            return true;
                        }
                    }

                    break;
                }
                case ':': {  // DSV table (:===) or document attribute (:name:)
                    lexer->advance(lexer, false);
                    lexer->markEnd(lexer);  // document-attr marker is just the colon
                    if(valid_symbols[TOKEN_DSV_TABLE_BLOCK_MARKER] && lexer->lookahead == '=') {
                        // Count the `=` run by hand rather than with consume(),
                        // which would move mark_end past the colon and corrupt
                        // the document-attr fallback below.
                        usize counter = 0;
                        while(lexer->lookahead == '=') {
                            lexer->advance(lexer, false);
                            ++counter;
                        }
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, counter)) {
                                    lexer->markEnd(lexer);
                                    lexer->resultSymbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->markEnd(lexer);
                                lexer->resultSymbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_DSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    if(valid_symbols[TOKEN_DOCUMENT_ATTR_MARKER]) {
                        lexer->resultSymbol = TOKEN_DOCUMENT_ATTR_MARKER;
                        return true;
                    }
                    break;
                }
                case '[': {
                    if(valid_symbols[TOKEN_ELEMENT_ATTR_MARKER]) {
                        lexer->advance(lexer, false);
                        if(lexer->lookahead == '[' || lexer->lookahead == '#') {
                            return false;
                        }
                        lexer->markEnd(lexer);
                        lexer->resultSymbol = TOKEN_ELEMENT_ATTR_MARKER;
                        return true;
                    }
                    break;
                }
                case '\'': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('\'', lexer)) {
                            lexer->resultSymbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    break;
                }
                case '<': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('<', lexer)) {
                            lexer->resultSymbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_CALLOUT_LIST_MARKER]) {
                        if(lexer->getColumn(lexer) == 1) {
                            if(parse_sequence(lexer, ".>")) {
                                lexer->markEnd(lexer);
                                lexer->resultSymbol = TOKEN_CALLOUT_LIST_MARKER;
                                if(is_white_space(lexer->lookahead)) {
                                    return true;
                                }
                            }
                            if(parse_number(lexer)) {
                                if(lexer->lookahead == '>') {
                                    lexer->advance(lexer, false);
                                    lexer->markEnd(lexer);
                                    if(is_white_space(lexer->lookahead)) {
                                        lexer->resultSymbol = TOKEN_CALLOUT_LIST_MARKER;
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case ',': {  // CSV table
                    if(valid_symbols[TOKEN_CSV_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, counter)) {
                                    lexer->markEnd(lexer);
                                    lexer->resultSymbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->markEnd(lexer);
                                lexer->resultSymbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_CSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '|': {  // Table
                    if(valid_symbols[TOKEN_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_TABLE, counter)) {
                                    lexer->markEnd(lexer);
                                    lexer->resultSymbol = TOKEN_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->markEnd(lexer);
                                lexer->resultSymbol = TOKEN_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '!': {  // NTable
                    if(valid_symbols[TOKEN_NTABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        consume('=', lexer, false, NULL, USIZE_MAX);
                        if(is_newline(lexer->lookahead)) {
                            lexer->markEnd(lexer);
                            lexer->resultSymbol = TOKEN_NTABLE_BLOCK_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '/': {
                    if(valid_symbols[TOKEN_LINE_COMMENT_MARKER] ||
                       valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                        if(parse_sequence(lexer, "//")) {
                            lexer->markEnd(lexer);
                            if(valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                                if(parse_sequence(lexer, "//")) {
                                    if(is_newline(lexer->lookahead)) {
                                        lexer->resultSymbol = TOKEN_BLOCK_COMMENT_START_MARKER;
                                        lexer->markEnd(lexer);
                                        return true;
                                    }
                                }
                            }
                            lexer->resultSymbol = TOKEN_LINE_COMMENT_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '_': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_START_MARKER] || valid_symbols[TOKEN_QUOTED_BLOCK_END_MARKER]) {
                        consume('_', lexer, false, NULL, USIZE_MAX);
                        lexer->markEnd(lexer);
                        usize counter = lexer->getColumn(lexer);
                        if(counter >= 4 && (is_newline(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_QUOTED, counter)) {
                                scanner_pop(s);
                                lexer->resultSymbol = TOKEN_QUOTED_BLOCK_END_MARKER;
                            } else {
                                scanner_push(s, BLOCK_KIND_QUOTED, counter);
                                lexer->resultSymbol = TOKEN_QUOTED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }
                    break;
                }
                case '>': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_MD_MARKER]) {
                        lexer->advance(lexer, false);
                        lexer->markEnd(lexer);
                        lexer->resultSymbol = TOKEN_QUOTED_BLOCK_MD_MARKER;
                        if(is_white_space(lexer->lookahead) || is_eof(lexer) || is_newline(lexer->lookahead)) {
                            return true;
                        }
                    }
                    break;
                }
                case '+': {
                    lexer->advance(lexer, false);
                    if(valid_symbols[TOKEN_LIST_CONTINUATION]) {
                        if(is_newline(lexer->lookahead)) {
                            lexer->resultSymbol = TOKEN_LIST_CONTINUATION;
                            lexer->markEnd(lexer);
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_PASSTHROUGH_BLOCK_MARKER]) {
                        if(parse_sequence(lexer, "+++")) {
                            lexer->markEnd(lexer);
                            lexer->resultSymbol = TOKEN_PASSTHROUGH_BLOCK_MARKER;
                            if(is_new_line(lexer->lookahead) || is_eof(lexer)) {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        if(
            lexer->getColumn(lexer) == 0 && valid_symbols[TOKEN_IDENT_MARKER] && is_white_space(lexer->lookahead)
        ) {
            skip_white_space(lexer);
            if(!is_new_line(lexer->lookahead) && !is_eof(lexer)) {
                lexer->markEnd(lexer);
                lexer->resultSymbol = TOKEN_IDENT_MARKER;
                return true;
            }
        }

        // Description-list term: the run of text at the start of a line that
        // is terminated by a description marker (`::`/`:::`/`::::`/`;;`
        // followed by whitespace or a line break), e.g. the `CPU` in
        // `CPU:: the brain`.  This is probed last, so every other line-start
        // construct gets first refusal; the emitted token spans from column 0
        // (where the scan began) to just before the delimiter run regardless
        // of how far earlier probes advanced the cursor.  When no marker is
        // found it returns false and the line stays an ordinary paragraph.
        if(valid_symbols[TOKEN_TERM] && !scanner_is_matching_raw_block(s)) {
            usize term_len = 0;
            while(!is_newline(lexer->lookahead) && !is_eof(lexer)) {
                i32 c = lexer->lookahead;
                if((c == ':' || c == ';') && term_len > 0) {
                    lexer->markEnd(lexer);
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool valid_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
                        lexer->resultSymbol = TOKEN_TERM;
                        return true;
                    }
                    term_len += run;
                    continue;
                }
                lexer->advance(lexer, false);
                ++term_len;
            }
            return false;
        }
    }

    // Description-list marker: `::`, `:::`, `::::` or `;;`, always followed
    // by whitespace or a line break.  It is only valid once a term has been
    // read, i.e. mid-line, so the column-0 block above is skipped and the
    // lexer sits on the delimiter run.  A run that is not a valid marker
    // (e.g. a lone `:` in a URL or `a::b` without trailing space) returns
    // false so the delimiter falls back to ordinary term text.
    if(valid_symbols[TOKEN_DESCRIPTION_MARKER] &&
       (lexer->lookahead == ':' || lexer->lookahead == ';')) {
        i32 delim = lexer->lookahead;
        usize run = 0;
        while(lexer->lookahead == delim) {
            lexer->advance(lexer, false);
            ++run;
        }
        bool valid_run = (delim == ':' && run >= 2 && run <= 4) || (delim == ';' && run == 2);
        if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
            lexer->markEnd(lexer);
            lexer->resultSymbol = TOKEN_DESCRIPTION_MARKER;
            return true;
        }
        return false;
    }

    if(valid_symbols[TOKEN_CALLOUT_MARKER]) {
        parse_sequence(lexer, "#");
        parse_sequence(lexer, "//");
        parse_sequence(lexer, ";;");
        skip_white_space(lexer);

        if(parse_sequence(lexer, "<.>")) {
            lexer->markEnd(lexer);
            if(is_newline(lexer->lookahead)) {
                lexer->resultSymbol = TOKEN_CALLOUT_MARKER;
                return true;
            }
        }

        if(parse_number(lexer)) {
            if(lexer->lookahead == '>') {
                lexer->advance(lexer, false);
                lexer->markEnd(lexer);
                if(is_newline(lexer->lookahead)) {
                    lexer->resultSymbol = TOKEN_CALLOUT_MARKER;
                    return true;
                }
            }
        }

        if(parse_sequence(lexer, "!--")) {
            if(lexer->lookahead == '.' || parse_number(lexer)) {
                if(parse_sequence(lexer, "-->")) {
                    lexer->markEnd(lexer);
                    if(is_new_line(lexer->lookahead)) {
                        lexer->resultSymbol = TOKEN_CALLOUT_MARKER;
                        return true;
                    }
                }
            }
        }
    }

    if(
        start_pos == lexer->getColumn(lexer) &&
        valid_symbols[TOKEN_CELL_ATTR]
    ) {
        bool has_token = false;
        while(parse_table_attr(lexer)) {
            has_token = true;
        }

        if(has_token) {
            if(lexer->lookahead == '|' || lexer->lookahead == '!') {
                lexer->resultSymbol = TOKEN_CELL_ATTR;
                lexer->markEnd(lexer);
                return true;
            }
        }
    }

    return false;
}

bool parse_ordered_marker(Lexer *lexer) {
    if(lexer->getColumn(lexer) != 0) {
        return false;
    }

    if(is_ascii_digit(lexer->lookahead)) {
        while(is_ascii_digit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        lexer->resultSymbol = TOKEN_LIST_MARKER_DIGIT;
    } else if(is_ascii_alpha_lower(lexer->lookahead)) {
        lexer->resultSymbol = TOKEN_LIST_MARKER_ALPHA;
        lexer->advance(lexer, false);
    } else if(is_geek_lower(lexer->lookahead)) {
        lexer->resultSymbol = TOKEN_LIST_MARKER_GEEK;
        lexer->advance(lexer, false);
    } else {
        return false;
    }

    if(lexer->lookahead != '.') {
        return false;
    }
    lexer->advance(lexer, false);
    if(!is_white_space(lexer->lookahead)) {
        return false;
    }
    lexer->markEnd(lexer);
    return true;
}

static inline bool is_white_space(i32 ch) {
    return ch == ' ' || ch == '\t';
}

static inline bool is_ascii_digit(i32 ch) {
    return ch >= '0' && ch <= '9';
}

static inline bool is_ascii_alpha_lower(i32 ch) {
    return ch >= 'a' && ch <= 'z';
}

static inline bool is_geek_lower(i32 ch) {
    return ch >= 945 && ch <= 969;
}

static inline bool parse_breaks(char start, Lexer *lexer) {
    i32 counter = 0;

    while(lexer->lookahead == start) {
        lexer->advance(lexer, false);
        skip_white_space(lexer);
        ++counter;
    }

    lexer->markEnd(lexer);

    return counter >= 3 && is_newline(lexer->lookahead);
}

static inline bool skip_white_space(Lexer *lexer) {
    bool has_skiped = false;
    while(is_white_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_skiped = true;
    }

    return has_skiped;
}

static inline bool is_newline(i32 ch) {
    return ch == '\r' || ch == '\n';
}

static inline bool is_eof(Lexer *lexer) {
    return lexer->eof(lexer);
}

static inline bool is_new_line(i32 ch) {
    return ch == '\r' || ch == '\n';
}

static inline bool consume(i32 ch, Lexer *lexer, bool skip_space, usize *counter, usize max) {
    bool has_space = false;
    if(skip_space) {
        has_space |= skip_white_space(lexer);
    }

    while(lexer->lookahead == ch) {
        lexer->advance(lexer, false);
        if(skip_space) {
            has_space |= skip_white_space(lexer);
        }
        --max;
        if(counter != NULL) {
            *counter += 1;
        }
        if(max == 0) {
            break;
        }
    }

    lexer->markEnd(lexer);
    return has_space;
}

static inline bool parse_sequence_impl(Lexer *lexer, char const *sequence, usize len) {
    usize pos = 0;

    while(pos < len) {
        if(lexer->lookahead != sequence[pos]) {
            return false;
        }

        lexer->advance(lexer, false);
        ++pos;
    }

    return true;
}

static inline bool parse_number(Lexer *lexer) {
    bool has_number = false;
    while(is_ascii_digit(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_number = true;
    }
    return has_number;
}

char table_cell_attr_align_left = '<';
char table_cell_attr_align_right = '>';
char table_cell_attr_align_middle = '^';
char table_cell_attr_style_none = 'd';
char table_cell_attr_style_strong = 's';
char table_cell_attr_style_emphasis = 'e';
char table_cell_attr_style_monospaced = 'm';
char table_cell_attr_style_header = 'h';
char table_cell_attr_style_literal = 'l';
char table_cell_attr_style_asciidoc = 'a';

static inline bool parse_table_attr(Lexer *lexer) {
    if(lexer->lookahead == table_cell_attr_align_left ||
       lexer->lookahead == table_cell_attr_align_right ||
       lexer->lookahead == table_cell_attr_align_middle ||
       lexer->lookahead == table_cell_attr_style_none ||
       lexer->lookahead == table_cell_attr_style_strong ||
       lexer->lookahead == table_cell_attr_style_emphasis ||
       lexer->lookahead == table_cell_attr_style_monospaced ||
       lexer->lookahead == table_cell_attr_style_header ||
       lexer->lookahead == table_cell_attr_style_literal ||
       lexer->lookahead == table_cell_attr_style_asciidoc) {
        lexer->advance(lexer, false);
        return true;
    }
    // table_cell_span_vertical = /\.\d+\+/;
    if(lexer->lookahead == '.') {
        lexer->advance(lexer, false);
        return parse_number(lexer);
    }
    // table_cell_span_width = /\d+\+/;
    // table_cell_span_vw = /\d+\.\d+\+/;
    if(parse_number(lexer)) {
        if(lexer->lookahead == '.') {
            lexer->advance(lexer, false);
            if(!parse_number(lexer)) {
                return false;
            }
        }
        if(lexer->lookahead == '+') {
            lexer->advance(lexer, false);
            return true;
        }
    }

    return false;
}

static inline bool scanner_is_matching(Scanner const *self, BlockKind kind, usize counter) {
    Node *top = scanner_top(self);
    if(!top) {
        return false;
    }

    if(counter == 0) {
        return top->kind == kind;
    }
    return top->kind == kind && top->counter == counter;
}
static inline Result scanner_serialize(Scanner const *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    ret = static_cast<Result>(ret & quick_buffer_write_usize(qb, self->len));
    ret = static_cast<Result>(ret & quick_buffer_extend_bytes(qb, self->buffer, sizeof(Node) * self->len));

    return ret;
}

static inline Result scanner_deserialize(Scanner *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    usize len = 0;
    ret = static_cast<Result>(ret & quick_buffer_read_usize(qb, &len));
    if(self->capacity < len) {
        self->buffer = scanner_realloc(self->buffer, len * sizeof(Node));
        self->capacity = len;
    }
    self->len = len;
    ret = static_cast<Result>(ret & quick_buffer_read_bytes(qb, self->buffer, len * sizeof(Node)));

    return ret;
}

static inline bool scanner_pop_kind(Scanner *self, BlockKind kind, usize counter) {
    if(scanner_is_matching(self, kind, counter)) {
        scanner_pop(self);
        return true;
    }

    return false;
}

static inline void scanner_pop(Scanner *self) {
    if(self->len == 0) {
        return;
    }
    self->len -= 1;
}

static inline void scanner_push(Scanner *self, BlockKind kind, usize counter) {
    const usize BUFFER_SIZE = kSerializationBufferSize - sizeof(usize);

    if(self->len == self->capacity) {
        usize grow_counter = BUFFER_SIZE / sizeof(Node) - self->capacity;
        if(grow_counter == 0) {
            return;
        }
        if(LIST_GROW_SIZE < grow_counter) {
            grow_counter = LIST_GROW_SIZE;
        }

        self->capacity = self->capacity + grow_counter;
        self->buffer = scanner_realloc(self->buffer, sizeof(Node) * self->capacity);
    }

    Node *top = &(self->buffer[self->len++]);
    top->kind = kind;
    top->counter = counter;
}

static inline void scanner_init(Scanner *self) {
    bzero(self, sizeof(Scanner));
    self->buffer = scanner_malloc(LIST_INIT_SIZE * sizeof(Node));
    self->capacity = LIST_INIT_SIZE;
}

static inline void scanner_free(Scanner *self) {
    free(self->buffer);
    bzero(self, sizeof(Scanner));
}

static inline bool scanner_is_matching_raw_block(Scanner *self) {
    return scanner_is_matching(self, BLOCK_KIND_LISTING, 0) ||
           scanner_is_matching(self, BLOCK_KIND_LITERAL, 0);
}

static inline Node *scanner_top(Scanner const *self) {
    if(self->len == 0) {
        return NULL;
    }

    return &(self->buffer[self->len - 1]);
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::asciidoc
