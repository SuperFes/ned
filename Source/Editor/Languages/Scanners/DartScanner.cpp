// The dart external scanner, ported from https://github.com/UserNobody14/tree-sitter-dart (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::dart {

using namespace ned::editor::parse::scanner;

enum TokenType {
  TEMPLATE_CHARS_SINGLE,
  TEMPLATE_CHARS_DOUBLE,
  TEMPLATE_CHARS_SINGLE_SINGLE,
  TEMPLATE_CHARS_DOUBLE_SINGLE,
  TEMPLATE_CHARS_RAW_SLASH,
  BLOCK_COMMENT,
  DOCUMENTATION_BLOCK_COMMENT,
};

void *Create() { return NULL; }
static void Destroy(void *p_) {
    VoidPtr p{p_};}
void tree_sitter_dart_external_scanner_reset(void *p) {}
static unsigned Serialize(void *p_, char *buffer) {
    VoidPtr p{p_}; return 0; }
static void Deserialize(void *p_, const char *b, unsigned n) {
    VoidPtr p{p_};}

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }
static void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static bool scan_multiline_comments(Lexer *lexer) {

    bool documentation_comment = false;
    advance(lexer);
    if (lexer->lookahead != '*') return false;
    advance(lexer);
    if (lexer->lookahead == '*') documentation_comment = true;

    bool after_star = false;
    unsigned nesting_depth = 1;
    for (;;) {
      switch (lexer->lookahead) {
        case '\0':
          return false;
        case '*':
          advance(lexer);
          after_star = true;
          break;
        case '/':
          if (after_star) {
            advance(lexer);
            after_star = false;
            nesting_depth--;
            if (nesting_depth == 0) {
              if (!documentation_comment) {
                lexer->resultSymbol = BLOCK_COMMENT;
              } else {
                lexer->resultSymbol = DOCUMENTATION_BLOCK_COMMENT;
              }
              return true;
            }
          } else {
            advance(lexer);
            after_star = false;
            if (lexer->lookahead == '*') {
              nesting_depth++;
              advance(lexer);
            }
          }
          break;
        default:
          advance(lexer);
          after_star = false;
          break;
      }
    }
  return false;
}

static bool scan_templates(Lexer *lexer, const bool *valid_symbols) {
  if(valid_symbols[TEMPLATE_CHARS_DOUBLE]) {
              lexer->resultSymbol = TEMPLATE_CHARS_DOUBLE;
  } else if (valid_symbols[TEMPLATE_CHARS_SINGLE]) {
              lexer->resultSymbol = TEMPLATE_CHARS_SINGLE;
  } else if (valid_symbols[TEMPLATE_CHARS_SINGLE_SINGLE]) {
              lexer->resultSymbol = TEMPLATE_CHARS_SINGLE_SINGLE;
  } else {
              lexer->resultSymbol = TEMPLATE_CHARS_DOUBLE_SINGLE;
  }
  for (bool has_content = false;; has_content = true) {
    lexer->markEnd(lexer);
    switch (lexer->lookahead) {
      case '\'':
      case '"':
        return has_content;
      case '\n':
        if (valid_symbols[TEMPLATE_CHARS_DOUBLE_SINGLE] || valid_symbols[TEMPLATE_CHARS_SINGLE_SINGLE]) return false;
        advance(lexer);
        break;
      case '\0':
        return false;
      case '$':
        return has_content;
      case '\\':
        if (valid_symbols[TEMPLATE_CHARS_RAW_SLASH]) {
            lexer->resultSymbol = TEMPLATE_CHARS_RAW_SLASH;
            advance(lexer);
        } else {
            return has_content;
        }
        break;
      default:
        advance(lexer);
    }
  }
  return true;
}

static bool Scan(void *payload_, Lexer *lexer,
                                                  const bool *valid_symbols) {
    VoidPtr payload{payload_};
  if (
      valid_symbols[TEMPLATE_CHARS_DOUBLE] ||
      valid_symbols[TEMPLATE_CHARS_SINGLE] ||
      valid_symbols[TEMPLATE_CHARS_DOUBLE_SINGLE] ||
      valid_symbols[TEMPLATE_CHARS_SINGLE_SINGLE]
  ) {
    return scan_templates(lexer, valid_symbols);
  }
  while (iswspace(lexer->lookahead)) lexer->advance(lexer, true);

  if (lexer->lookahead == '/') {
    return scan_multiline_comments(lexer);
  }
  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::dart
