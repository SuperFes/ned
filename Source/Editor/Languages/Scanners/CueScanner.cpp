// The cue external scanner, ported from https://github.com/eonpatapon/tree-sitter-cue (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::cue {

using namespace ned::editor::parse::scanner;

enum TokenType {
  MULTI_STR_CONTENT,
  MULTI_BYTES_CONTENT,
  RAW_STR_CONTENT,
  RAW_BYTES_CONTENT,
  MULTI_RAW_STR_CONTENT,
  MULTI_RAW_BYTES_CONTENT,
};

static void advance(Lexer *lexer) { lexer->advance(lexer, false); }

static void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static bool scan_multiline(Lexer *lexer, int c) {
  bool has_content = false;
  if (c == '"') {
    lexer->resultSymbol = MULTI_STR_CONTENT;
  } else if (c == '\'') {
    lexer->resultSymbol = MULTI_BYTES_CONTENT;
  }

  while (true) {
    switch (lexer->lookahead) {
    case '\'':
    case '"':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == c) {
        advance(lexer);
        if (lexer->lookahead == c) {
          if (has_content) {
            return true;
          } else {
            return false;
          }
        }
      }
      break;
    case '\\':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == '(') {
        if (has_content) {
          return true;
        } else {
          return false;
        }
      } else {
        // FIXME: Accept anything after '\'
        advance(lexer);
      }
      has_content = true;
      break;
    case '\0':
      if (lexer->eof(lexer)) {
        return false;
      }
      advance(lexer);
      has_content = true;
      break;
    default:
      advance(lexer);
      has_content = true;
      break;
    }
  }
}

static bool scan_raw_multiline(Lexer *lexer, int c) {
  bool has_content = false;
  if (c == '"') {
    lexer->resultSymbol = MULTI_RAW_STR_CONTENT;
  } else if (c == '\'') {
    lexer->resultSymbol = MULTI_RAW_BYTES_CONTENT;
  }

  while (true) {
    switch (lexer->lookahead) {
    case '\'':
    case '"':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == c) {
        advance(lexer);
        if (lexer->lookahead == c) {
          advance(lexer);
          if (lexer->lookahead == '#') {
            if (has_content) {
              return true;
            } else {
              return false;
            }
          }
        }
      }
      break;
    case '\\':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == '#') {
        advance(lexer);
        if (lexer->lookahead == '(') {
          if (has_content) {
            return true;
          } else {
            return false;
          }
        }
      }
      has_content = true;
      break;
    case '\0':
      if (lexer->eof(lexer)) {
        return false;
      }
      advance(lexer);
      has_content = true;
      break;
    default:
      advance(lexer);
      has_content = true;
      break;
    }
  }
}

static bool scan_raw(Lexer *lexer, int c) {
  bool has_content = false;
  if (c == '"') {
    lexer->resultSymbol = RAW_STR_CONTENT;
  } else if (c == '\'') {
    lexer->resultSymbol = RAW_BYTES_CONTENT;
  }

  while (true) {
    switch (lexer->lookahead) {
    case '\'':
    case '"':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == '#') {
        if (has_content) {
          return true;
        } else {
          return false;
        }
      }
      break;
    case '\\':
      lexer->markEnd(lexer);
      advance(lexer);
      if (lexer->lookahead == '#') {
        advance(lexer);
        if (lexer->lookahead == '(') {
          if (has_content) {
            return true;
          } else {
            return false;
          }
        }
      } else {
        advance(lexer);
      }
      has_content = true;
      break;
    case '\0':
      if (lexer->eof(lexer)) {
        return false;
      }
      advance(lexer);
      has_content = true;
      break;
    default:
      advance(lexer);
      has_content = true;
      break;
    }
  }
}

static bool scan(Lexer *lexer, const bool *valid_symbols) {
  if (valid_symbols[MULTI_STR_CONTENT]) {
    return scan_multiline(lexer, '"');
  } else if (valid_symbols[MULTI_BYTES_CONTENT]) {
    return scan_multiline(lexer, '\'');
  } else if (valid_symbols[MULTI_RAW_STR_CONTENT]) {
    return scan_raw_multiline(lexer, '"');
  } else if (valid_symbols[MULTI_RAW_BYTES_CONTENT]) {
    return scan_raw_multiline(lexer, '\'');
  } else if (valid_symbols[RAW_STR_CONTENT]) {
    return scan_raw(lexer, '"');
  } else if (valid_symbols[RAW_BYTES_CONTENT]) {
    return scan_raw(lexer, '\'');
  }

  return false;
}

#ifdef __cplusplus
extern "C" {
#endif

void *Create() { return NULL; }

static bool Scan(void *payload_, Lexer *lexer,
                                           const bool *valid_symbols) {
    VoidPtr payload{payload_};
  return scan(lexer, valid_symbols);
}

static unsigned Serialize(void *payload_,
                                                    char *buffer) {
    VoidPtr payload{payload_};
  return 0;
}

static void Deserialize(void *payload_,
                                                  const char *buffer,
                                                  unsigned length) {
    VoidPtr payload{payload_};}

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

#ifdef __cplusplus
}
#endif

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::cue

NED_SCANNER_LIBRARY_EXPORT(cue, ned::editor::languages::scanners::cue::kScanner)
