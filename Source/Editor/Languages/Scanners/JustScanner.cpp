// The just external scanner, ported from https://github.com/IndianBoy42/tree-sitter-just (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::just {

using namespace ned::editor::parse::scanner;

// Enable this for debugging
// #define DEBUG_PRINT

#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

#ifdef __GNUC__
#define unused_attr __attribute__((unused))
#else
#define unused_attr
#endif

// Upstream aborts the process on a scanner invariant failure outside
// wasm builds; inside an editor a failed invariant is a scan that returns
// no token, never an exit, so the wasm no-op is the one ned keeps.
#define assertf(...) (void)0;
#define dbg_print(...)

#define SBYTES sizeof(Scanner)

enum TokenType {
  INDENT,
  DEDENT,
  NEWLINE,
  TEXT,
  ERROR_RECOVERY,
  TOKEN_TYPE_END,
};

unused_attr static inline void assert_valid_token(const Symbol sym) {
  assertf(sym >= INDENT && sym < TOKEN_TYPE_END, "invalid symbol %d", sym);
}

typedef struct Scanner {
  uint32_t prev_indent;
  uint16_t advance_brace_count;
  bool has_seen_eof;
} Scanner;

// This function should create your scanner object. It will only be called once
// anytime your language is set on a parser. Often, you will want to allocate
// memory on the heap and return a pointer to it. If your external scanner
// doesn’t need to maintain any state, it’s ok to return NULL.
void *Create(void) {
  Scanner *ptr = (Scanner *)scanner_calloc(SBYTES, 1);
  assertf(ptr, "Create: out of memory");
  return ptr;
}

// This function should free any memory used by your scanner. It is called once
// when a parser is deleted or assigned a different language. It receives as an
// argument the same pointer that was returned from the create function. If your
// create function didn’t allocate any memory, this function can be a noop.
static void Destroy(void *payload_) {
    VoidPtr payload{payload_};
  assertf(payload, "got null payload at destroy");
  free(payload);
}

// Serialize the state of the scanner. This is called when the parser is
// serialized. It receives as an argument the same pointer that was returned
// from the create function.
static unsigned Serialize(void *payload_,
                                                     char *buffer) {
    VoidPtr payload{payload_};
  assertf(SBYTES < kSerializationBufferSize,
          "invalid scanner size");
  memcpy(buffer, payload, SBYTES);
  return SBYTES;
}

// Reconstruct a scanner from the serialized state. This is called when the
// parser is deserialized.
static void Deserialize(void *payload_,
                                                   const char *buffer,
                                                   unsigned length) {
    VoidPtr payload{payload_};
  Scanner *ptr = (Scanner *)payload;
  if (length == 0) {
    ptr->prev_indent = 0;
    ptr->has_seen_eof = false;
    return;
  }
  memcpy(ptr, buffer, SBYTES);
}

// Continue and include the preceding character in the token
static inline void advance(Lexer *lexer) { lexer->advance(lexer, false); }

// Continue and discard the preceding character
static inline void skip(Lexer *lexer) { lexer->advance(lexer, true); }

// An EOF works as a dedent
static bool handle_eof(Lexer *lexer, Scanner *state,
                       const bool *valid_symbols) {
  assertf(lexer->eof(lexer), "expected EOF");
  lexer->markEnd(lexer);

  if (valid_symbols[DEDENT]) {
    lexer->resultSymbol = DEDENT;
    return true;
  }

  if (valid_symbols[NEWLINE]) {
    if (state->has_seen_eof) {
      // allow EOF to count for a single symbol. Don't return true more than
      // once, otherwise it will keep calling us thinking there are more tokens.
      return false;
    }

    lexer->resultSymbol = NEWLINE;
    state->has_seen_eof = true;
    return true;
  }
  return false;
}

// This function is responsible for recognizing external tokens. It should
// return true if a token was recognized, and false otherwise.
static bool Scan(void *payload_, Lexer *lexer,
                                            const bool *valid_symbols) {
    VoidPtr payload{payload_};
  Scanner *scanner = (Scanner *)(payload);

  if (lexer->eof(lexer)) {
    return handle_eof(lexer, scanner, valid_symbols);
  }

  // Handle backslash escaping for newlines
  if (valid_symbols[NEWLINE]) {
    bool escape = false;
    if (lexer->lookahead == '\\') {
      escape = true;
      skip(lexer);
    }

    bool eol_found = false;
    while (iswspace(lexer->lookahead)) {
      if (lexer->lookahead == '\n') {
        skip(lexer);
        eol_found = true;
        break;
      }
      skip(lexer);
    }

    if (eol_found && !escape) {
      lexer->resultSymbol = NEWLINE;
      return true;
    }
  }

  if (valid_symbols[INDENT] || valid_symbols[DEDENT]) {
    while (!lexer->eof(lexer) && isspace(lexer->lookahead)) {
      switch (lexer->lookahead) {
      case '\n':
        if (valid_symbols[INDENT]) {
          return false;
        }

      case '\t':
      case ' ':
        skip(lexer);
        break;

      default:
        return false;
      }
    }

    if (lexer->eof(lexer)) {
      return handle_eof(lexer, scanner, valid_symbols);
    }

    uint32_t indent = lexer->getColumn(lexer);

    if (indent > scanner->prev_indent && valid_symbols[INDENT] &&
        scanner->prev_indent == 0) {
      lexer->resultSymbol = INDENT;
      scanner->prev_indent = indent;
      return true;
    }
    if (indent < scanner->prev_indent && valid_symbols[DEDENT] && indent == 0) {
      lexer->resultSymbol = DEDENT;
      scanner->prev_indent = indent;
      return true;
    }
  }

  if (valid_symbols[TEXT]) {
    if (lexer->getColumn(lexer) == scanner->prev_indent &&
        (lexer->lookahead == '\n' || lexer->lookahead == '@' ||
         lexer->lookahead == '-')) {
      return false;
    }

    bool advanced_once = false;

    while (lexer->lookahead == '{' && scanner->advance_brace_count > 0 &&
           !lexer->eof(lexer)) {
      scanner->advance_brace_count--;
      advance(lexer);
      advanced_once = true;
    }

    while (1) {
      if (lexer->eof(lexer)) {
        return handle_eof(lexer, scanner, valid_symbols);
      }

      while (!lexer->eof(lexer) && lexer->lookahead != '\n' &&
             lexer->lookahead != '{') {
        // Can't start with #!
        if (lexer->lookahead == '#' && !advanced_once) {
          advance(lexer);
          if (lexer->lookahead == '!') {
            return false;
          }
        }

        advance(lexer);
        advanced_once = true;
      }

      if (lexer->lookahead == '\n' || lexer->eof(lexer)) {
        lexer->markEnd(lexer);
        lexer->resultSymbol = TEXT;
        if (advanced_once) {
          return true;
        }
        if (lexer->eof(lexer)) {
          return handle_eof(lexer, scanner, valid_symbols);
        }
        advance(lexer);
      } else if (lexer->lookahead == '{') {
        lexer->markEnd(lexer);
        advance(lexer);

        if (lexer->eof(lexer) ||
            lexer->lookahead == '\n') { // EOF without anything after {
          lexer->markEnd(lexer);
          lexer->resultSymbol = TEXT;
          return advanced_once;
        }

        if (lexer->lookahead == '{') {
          advance(lexer);

          while (lexer->lookahead == '{') { // more braces!
            scanner->advance_brace_count++;
            advance(lexer);
          }

          // scan till a balanced pair of }} are found, then assume it's a valid
          // interpolation
          while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
            advance(lexer);
            if (lexer->lookahead == '}') {
              advance(lexer);
              if (lexer->lookahead == '}') {
                lexer->resultSymbol = TEXT;
                return advanced_once;
              }
            }
          }

          if (!advanced_once) {
            return false;
          }
        }
      }
    }
  }

  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::just
