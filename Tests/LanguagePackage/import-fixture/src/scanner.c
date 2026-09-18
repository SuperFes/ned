#include "tree_sitter/parser.h"

void *tree_sitter_words_external_scanner_create() { return NULL; }
void tree_sitter_words_external_scanner_destroy(void *payload) {}
unsigned tree_sitter_words_external_scanner_serialize(void *payload, char *buffer) { return 0; }
void tree_sitter_words_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {}
bool tree_sitter_words_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) { return false; }
