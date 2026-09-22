// The pkl external scanner, ported from https://github.com/apple/tree-sitter-pkl (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::pkl {

using namespace ned::editor::parse::scanner;

/*
 * Copyright © 2024-2026 Apple Inc. and the Pkl project authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

enum TokenType {
  // sequence of "normal" characters in a single line string without a pound sign
  SL_STRING_CHARS,
  // sequence of "normal" characters in single line string with one pound sign
  SL1_STRING_CHARS,
  // sequence of "normal" characters in single line string with two pound signs
  SL2_STRING_CHARS,
  // sequence of "normal" characters in single line string with three pound signs
  SL3_STRING_CHARS,
  // sequence of "normal" characters in single line string with four pound signs
  SL4_STRING_CHARS,
  // sequence of "normal" characters in single line string with five pound signs
  SL5_STRING_CHARS,
  // sequence of "normal" characters in single line string with six pound signs
  SL6_STRING_CHARS,
  // sequence of "normal" characters in multiline string without pound sign
  ML_STRING_CHARS,
  // sequence of "normal" characters in multiline string with one pound sign
  ML1_STRING_CHARS,
  // sequence of "normal" characters in multiline string with two pound signs
  ML2_STRING_CHARS,
  // sequence of "normal" characters in multiline string with three pound signs
  ML3_STRING_CHARS,
  // sequence of "normal" characters in multiline string with four pound signs
  ML4_STRING_CHARS,
  // sequence of "normal" characters in multiline string with five pound signs
  ML5_STRING_CHARS,
  // sequence of "normal" characters in multiline string with six pound signs
  ML6_STRING_CHARS,
  // '[' at the start of a subscript
  OPEN_SUBSCRIPT_BRACKET,
  // '(' at the start of a method call, or a type constraint
  OPEN_ARGUMENT_PAREN,
  // binary minus ('-') operator
  BINARY_MINUS
};

void *Create() { return NULL; }
static void Destroy(void *p_) {
    VoidPtr p{p_};}
void tree_sitter_pkl_external_scanner_reset(void *p) {}
static unsigned Serialize(void *p_, char *buffer) {
    VoidPtr p{p_}; return 0; }
static void Deserialize(void *p_, const char *b, unsigned n) {
    VoidPtr p{p_};}

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }
static void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static bool parse_sl_string_chars(Lexer *lexer) {
  bool has_content = false;
  while (true) {
    switch (lexer->lookahead) {
      case '"':
      case '\\':
        return has_content;
      case '\n':
      case '\r':
      case 0:
        return has_content;
      default:
        has_content = true;
        advance(lexer);
    }
  }
}

static bool parse_slx_string_chars(Lexer *lexer, int num_pounds) {
  bool has_content = false;
  switch(num_pounds) {
    case 1:
      lexer->resultSymbol = SL1_STRING_CHARS;
      break;
    case 2:
      lexer->resultSymbol = SL2_STRING_CHARS;
      break;
    case 3:
      lexer->resultSymbol = SL3_STRING_CHARS;
      break;
    case 4:
      lexer->resultSymbol = SL4_STRING_CHARS;
      break;
    case 5:
      lexer->resultSymbol = SL5_STRING_CHARS;
      break;
    case 6:
      lexer->resultSymbol = SL6_STRING_CHARS;
      break;
    default:
      lexer->resultSymbol = SL6_STRING_CHARS;
      break;
  }

  while (true) {
    next_iter:
    switch (lexer->lookahead) {
      case '"':
      case '\\':
        lexer->markEnd(lexer);
        advance(lexer);
        for (int i = 0; i < num_pounds; i++) {
          if (lexer->lookahead != '#') {
            has_content = true;
            goto next_iter;
          }
          advance(lexer);
        }
        return has_content;
      case '\n':
      case '\r':
      case 0:
        lexer->markEnd(lexer);
        return has_content;
      default:
        has_content = true;
        advance(lexer);
    }
  }
}

static bool parse_ml_string_chars(Lexer *lexer) {
  bool has_content = false;
  lexer->resultSymbol = ML_STRING_CHARS;

  while (true) {
    switch (lexer->lookahead) {
      case '"':
        lexer->markEnd(lexer);
        advance(lexer);
        if (lexer->lookahead == '"') {
          advance(lexer);
          if (lexer->lookahead == '"') {
            return has_content;
          }
        }
        has_content = true;
        break;
      case '\\':
      case 0:
        lexer->markEnd(lexer);
        return has_content;
      default:
        has_content = true;
        advance(lexer);
    }
  }
}

static bool parse_mlx_string_chars(Lexer *lexer, int num_pounds) {
  bool has_content = false;
  switch(num_pounds) {
    case 1:
      lexer->resultSymbol = ML1_STRING_CHARS;
      break;
    case 2:
      lexer->resultSymbol = ML2_STRING_CHARS;
      break;
    case 3:
      lexer->resultSymbol = ML3_STRING_CHARS;
      break;
    case 4:
      lexer->resultSymbol = ML4_STRING_CHARS;
      break;
    case 5:
      lexer->resultSymbol = ML5_STRING_CHARS;
      break;
    case 6:
      lexer->resultSymbol = ML6_STRING_CHARS;
      break;
    default:
      lexer->resultSymbol = ML6_STRING_CHARS;
      break;
  }

  while (true) {
    next_iter:
    switch (lexer->lookahead) {
      case '"': {
        lexer->markEnd(lexer);
        int quote_count = 0;
        do {
          quote_count += 1;
          advance(lexer);
        } while (lexer->lookahead == '"');
        if (quote_count < 3) {
          has_content = true;
          break;
        }
        for (int i = 0; i < num_pounds; i++) {
          if (lexer->lookahead != '#') {
            has_content = true;
            goto next_iter;
          }
          advance(lexer);
        }
        return has_content;
      }
      case '\\':
        lexer->markEnd(lexer);
        advance(lexer);
        for (int i = 0; i < num_pounds; i++) {
          if (lexer->lookahead != '#') {
            has_content = true;
            goto next_iter;
          }
          advance(lexer);
        }
        return has_content;
      case 0:
        lexer->markEnd(lexer);
        return has_content;
      default:
        has_content = true;
        advance(lexer);
    }
  }
}

static bool parse_symbol_no_preceding_newline_or_semicolon(Lexer *lexer, bool open_subscript_bracket, bool open_argument_paren, bool binary_minus) {
  if (lexer->eof(lexer)) {
    return false;
  }
  while (true) {
    switch (lexer->lookahead) {
      case ' ':
      case '\t':
      case '\r':
      case '\f':
        skip(lexer);
        break;
      case '[':
        if (open_subscript_bracket) {
          advance(lexer);
          lexer->resultSymbol = OPEN_SUBSCRIPT_BRACKET;
          return true;
        } else {
          return false;
        }
      case '(':
        if (open_argument_paren) {
          advance(lexer);
          lexer->resultSymbol = OPEN_ARGUMENT_PAREN;
          return true;
        } else {
          return false;
        }
      case '-':
        if (binary_minus) {
          advance(lexer);
          lexer->markEnd(lexer);
          // avoid parsing `->` as binary minus
          if (lexer->lookahead == '>') {
            return false;
          }
          lexer->resultSymbol = BINARY_MINUS;
          return true;
        } else {
          return false;
        }
      default:
        return false;
    }
  }
}

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
  bool sl = valid_symbols[SL_STRING_CHARS];
  bool sl1 = valid_symbols[SL1_STRING_CHARS];
  bool sl2 = valid_symbols[SL2_STRING_CHARS];
  bool sl3 = valid_symbols[SL3_STRING_CHARS];
  bool sl4 = valid_symbols[SL4_STRING_CHARS];
  bool sl5 = valid_symbols[SL5_STRING_CHARS];
  bool sl6 = valid_symbols[SL6_STRING_CHARS];
  bool ml = valid_symbols[ML_STRING_CHARS];
  bool ml1 = valid_symbols[ML1_STRING_CHARS];
  bool ml2 = valid_symbols[ML2_STRING_CHARS];
  bool ml3 = valid_symbols[ML3_STRING_CHARS];
  bool ml4 = valid_symbols[ML4_STRING_CHARS];
  bool ml5 = valid_symbols[ML5_STRING_CHARS];
  bool ml6 = valid_symbols[ML6_STRING_CHARS];
  bool osb = valid_symbols[OPEN_SUBSCRIPT_BRACKET];
  bool oap = valid_symbols[OPEN_ARGUMENT_PAREN];
  bool bminus = valid_symbols[BINARY_MINUS];

  if (sl && sl1 && sl2 && sl3 && sl4 && sl5 && sl6 && ml && ml1 && ml2 && ml3 && ml4 && ml5 && ml6 && osb && oap && bminus) {
    // error recovery mode -> don't match any string chars
    return false;
  }

  if (sl) {
    return parse_sl_string_chars(lexer);
  }
  if (ml) {
    return parse_ml_string_chars(lexer);
  }
  if (sl1) {
    return parse_slx_string_chars(lexer, 1);
  }
  if (ml1) {
    return parse_mlx_string_chars(lexer, 1);
  }
  if (sl2) {
    return parse_slx_string_chars(lexer, 2);
  }
  if (ml2) {
    return parse_mlx_string_chars(lexer, 2);
  }
  if (sl3) {
    return parse_slx_string_chars(lexer, 3);
  }
  if (ml3) {
    return parse_mlx_string_chars(lexer, 3);
  }
  if (sl4) {
    return parse_slx_string_chars(lexer, 4);
  }
  if (ml4) {
    return parse_mlx_string_chars(lexer, 4);
  }
  if (sl5) {
    return parse_slx_string_chars(lexer, 5);
  }
  if (ml5) {
    return parse_mlx_string_chars(lexer, 5);
  }
  if (sl6) {
    return parse_slx_string_chars(lexer, 6);
  }
  if (ml6) {
    return parse_mlx_string_chars(lexer, 6);
  }
  // either possibly be true
  if (osb || oap || bminus) {
    return parse_symbol_no_preceding_newline_or_semicolon(lexer, osb, oap, bminus);
  }
  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::pkl

NED_SCANNER_LIBRARY_EXPORT(pkl, ned::editor::languages::scanners::pkl::kScanner)
