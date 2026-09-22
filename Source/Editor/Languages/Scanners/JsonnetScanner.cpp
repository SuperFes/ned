// The jsonnet external scanner, ported from https://github.com/sourcegraph/tree-sitter-jsonnet (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::jsonnet {

using namespace ned::editor::parse::scanner;

// https://github.com/Azganoth/tree-sitter-lua/blob/master/src/scanner.cc
// https://github.com/MunifTanjim/tree-sitter-lua/
// and now here

enum TokenType {
  STRING_START,
  STRING_CONTENT,
  STRING_END,
};

static inline void consume(Lexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static inline bool consume_char(char c, Lexer *lexer) {
  if (lexer->lookahead != c) {
    return false;
  }

  consume(lexer);
  return true;
}

static inline uint8_t consume_and_count_char(char c, Lexer *lexer) {
  uint8_t count = 0;
  while (lexer->lookahead == c) {
    ++count;
    consume(lexer);
  }
  return count;
}

static inline void skip_whitespaces(Lexer *lexer) {
  while (iswspace(lexer->lookahead)) {
    skip(lexer);
  }
}

void *Create() { return NULL; }
static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

enum InsideNode { INSIDE_NONE,  INSIDE_STRING };

uint8_t inside_node = INSIDE_NONE;
char ending_char = 0;
uint8_t level_count = 0;

static inline void reset_state() {
  inside_node = INSIDE_NONE;
  ending_char = 0;
  level_count = 0;
}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
  buffer[0] = inside_node;
  buffer[1] = ending_char;
  buffer[2] = level_count;
  return 3;
}

static void Deserialize(void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};
  if (length == 0) return;
  inside_node = buffer[0];
  if (length == 1) return;
  ending_char = buffer[1];
  if (length == 2) return;
  level_count = buffer[2];
}

static bool scan_block_start(Lexer *lexer) {
  if (consume_char('|', lexer) && consume_char('|', lexer) && consume_char('|', lexer)) {
    return true;
  }

  return false;
}

static bool scan_block_end(Lexer *lexer) {
  if (consume_char('|', lexer) && consume_char('|', lexer) && consume_char('|', lexer)) {
    return true;
  }

  return false;
}

static bool scan_block_content(Lexer *lexer) {
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '|') {
      lexer->markEnd(lexer);

      if (scan_block_end(lexer)) {
        return true;
      }
    } else {
      consume(lexer);
    }
  }

  return false;
}

static bool scan_string_start(Lexer *lexer) {
  if (lexer->lookahead == '"' || lexer->lookahead == '\'') {
    inside_node = INSIDE_STRING;
    ending_char = lexer->lookahead;
    consume(lexer);
    return true;
  }

  if (scan_block_start(lexer)) {
    inside_node = INSIDE_STRING;
    return true;
  }

  return false;
}

static bool scan_string_end(Lexer *lexer) {
  if (ending_char == 0) { // block string
    return scan_block_end(lexer);
  }

  if (consume_char(ending_char, lexer)) {
    return true;
  }

  return false;
}

static bool scan_string_content(Lexer *lexer) {
  if (ending_char == 0) { // block string
    return scan_block_content(lexer);
  }

  while (lexer->lookahead != '\n' && lexer->lookahead != 0 && lexer->lookahead != ending_char) {
    while (consume_char('\\', lexer) && consume_char('z', lexer)) continue;

    if (lexer->lookahead == 0) {
      return true;
    }

    consume(lexer);
  }

  return true;
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
  if (inside_node == INSIDE_STRING) {
    if (valid_symbols[STRING_END] && scan_string_end(lexer)) {
      reset_state();
      lexer->resultSymbol = STRING_END;
      return true;
    }

    if (valid_symbols[STRING_CONTENT] && scan_string_content(lexer)) {
      lexer->resultSymbol = STRING_CONTENT;
      return true;
    }

    return false;
  }

  skip_whitespaces(lexer);

  if (valid_symbols[STRING_START] && scan_string_start(lexer)) {
    lexer->resultSymbol = STRING_START;
    return true;
  }

  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::jsonnet

NED_SCANNER_LIBRARY_EXPORT(jsonnet, ned::editor::languages::scanners::jsonnet::kScanner)
