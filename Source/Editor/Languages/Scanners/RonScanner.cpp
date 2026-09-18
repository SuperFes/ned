// The ron external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-ron (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::ron {

using namespace ned::editor::parse::scanner;

enum TokenType {
  STRING_CONTENT,
  RAW_STRING,
  FLOAT,
  BLOCK_COMMENT,
};

void *Create() { return NULL; }
static void Destroy(void *p_) {
    VoidPtr p{p_};}
void tree_sitter_ron_external_scanner_reset(void *p) {}
static unsigned Serialize(void *p_, char *buffer) {
    VoidPtr p{p_};
  return 0;
}
static void Deserialize(void *p_, const char *b,
                                                  unsigned n) {
    VoidPtr p{p_};}

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static bool is_num_char(int32_t c) { return c == '_' || iswdigit(c); }

static bool Scan(void *payload_, Lexer *lexer,
                                           const bool *valid_symbols) {
    VoidPtr payload{payload_};
  if (valid_symbols[STRING_CONTENT] && !valid_symbols[FLOAT]) {
    bool has_content = false;
    for (;;) {
      if (lexer->lookahead == '\"' || lexer->lookahead == '\\') {
        break;
      } else if (lexer->lookahead == 0) {
        return false;
      }
      has_content = true;
      advance(lexer);
    }
    lexer->resultSymbol = STRING_CONTENT;
    return has_content;
  }

  while (iswspace(lexer->lookahead))
    lexer->advance(lexer, true);

  if (valid_symbols[RAW_STRING] &&
      (lexer->lookahead == 'r' || lexer->lookahead == 'b')) {
    lexer->resultSymbol = RAW_STRING;
    if (lexer->lookahead == 'b')
      advance(lexer);
    if (lexer->lookahead != 'r')
      return false;
    advance(lexer);

    unsigned opening_hash_count = 0;
    while (lexer->lookahead == '#') {
      advance(lexer);
      opening_hash_count++;
    }

    if (lexer->lookahead != '"')
      return false;
    advance(lexer);

    for (;;) {
      if (lexer->lookahead == 0) {
        return false;
      } else if (lexer->lookahead == '"') {
        advance(lexer);
        unsigned hash_count = 0;
        while (lexer->lookahead == '#' && hash_count < opening_hash_count) {
          advance(lexer);
          hash_count++;
        }
        if (hash_count == opening_hash_count) {
          return true;
        }
      } else {
        advance(lexer);
      }
    }
  }

  if (valid_symbols[FLOAT] && iswdigit(lexer->lookahead)) {
    lexer->resultSymbol = FLOAT;

    advance(lexer);
    while (is_num_char(lexer->lookahead)) {
      advance(lexer);
    }

    bool has_fraction = false, has_exponent = false;

    if (lexer->lookahead == '.') {
      has_fraction = true;
      advance(lexer);
      if (iswalpha(lexer->lookahead)) {
        // The dot is followed by a letter: 1.max(2) => not a float but an
        // integer
        return false;
      }

      if (lexer->lookahead == '.') {
        return false;
      }
      while (is_num_char(lexer->lookahead)) {
        advance(lexer);
      }
    }

    lexer->markEnd(lexer);

    if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
      has_exponent = true;
      advance(lexer);
      if (lexer->lookahead == '+' || lexer->lookahead == '-') {
        advance(lexer);
      }
      if (!is_num_char(lexer->lookahead)) {
        return true;
      }
      advance(lexer);
      while (is_num_char(lexer->lookahead)) {
        advance(lexer);
      }

      lexer->markEnd(lexer);
    }

    if (!has_exponent && !has_fraction)
      return false;

    if (lexer->lookahead != 'u' && lexer->lookahead != 'i' &&
        lexer->lookahead != 'f') {
      return true;
    }
    advance(lexer);
    if (!iswdigit(lexer->lookahead)) {
      return true;
    }

    while (iswdigit(lexer->lookahead)) {
      advance(lexer);
    }

    lexer->markEnd(lexer);
    return true;
  }

  if (lexer->lookahead == '/') {
    advance(lexer);
    if (lexer->lookahead != '*')
      return false;
    advance(lexer);

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
            lexer->resultSymbol = BLOCK_COMMENT;
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
  }

  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::ron
