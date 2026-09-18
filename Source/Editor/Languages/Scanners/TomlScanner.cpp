// The toml external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-toml (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::toml {

using namespace ned::editor::parse::scanner;

typedef enum {
    LINE_ENDING_OR_EOF,
    MULTILINE_BASIC_STRING_CONTENT,
    MULTILINE_BASIC_STRING_END,
    MULTILINE_LITERAL_STRING_CONTENT,
    MULTILINE_LITERAL_STRING_END,
} TokenType;

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

bool tree_sitter_toml_external_scanner_scan_multiline_string_end(Lexer* lexer, const bool* valid_symbols,
                                                                 int32_t delimiter, TokenType content_symbol,
                                                                 TokenType end_symbol) {
    if (!valid_symbols[end_symbol] || lexer->lookahead != delimiter) {
        return false;
    }

    lexer->advance(lexer, false);
    lexer->markEnd(lexer);

    if (lexer->lookahead != delimiter) {
        lexer->resultSymbol = content_symbol;
        return true;
    }

    lexer->advance(lexer, false);

    if (lexer->lookahead != delimiter) {
        lexer->markEnd(lexer);
        lexer->resultSymbol = content_symbol;
        return true;
    }

    lexer->advance(lexer, false);

    if (lexer->lookahead != delimiter) {
        lexer->markEnd(lexer);
        lexer->resultSymbol = end_symbol;
        return true;
    }

    lexer->resultSymbol = content_symbol;
    return true;
}

static bool Scan(void* payload_, Lexer* lexer, const bool* valid_symbols) {
    VoidPtr payload{payload_};
    if (tree_sitter_toml_external_scanner_scan_multiline_string_end(
            lexer, valid_symbols, '"', MULTILINE_BASIC_STRING_CONTENT, MULTILINE_BASIC_STRING_END) ||
        tree_sitter_toml_external_scanner_scan_multiline_string_end(
            lexer, valid_symbols, '\'', MULTILINE_LITERAL_STRING_CONTENT, MULTILINE_LITERAL_STRING_END)) {
        return true;
    }

    if (valid_symbols[LINE_ENDING_OR_EOF]) {
        lexer->resultSymbol = LINE_ENDING_OR_EOF;

        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }

        if (lexer->lookahead == 0 || lexer->lookahead == '\n') {
            return true;
        }

        if (lexer->lookahead == '\r') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '\n') {
                return true;
            }
        }
    }

    return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::toml
