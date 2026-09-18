// The latex external scanner, ported from https://github.com/latex-lsp/tree-sitter-latex (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::latex {

using namespace ned::editor::parse::scanner;

enum TokenType {
  TRIVIA_RAW_FI,
  TRIVIA_RAW_ENV_COMMENT,
  TRIVIA_RAW_ENV_VERBATIM,
  TRIVIA_RAW_ENV_LISTING,
  TRIVIA_RAW_ENV_MINTED,
  TRIVIA_RAW_ENV_ASY,
  TRIVIA_RAW_ENV_ASYDEF,
  TRIVIA_RAW_ENV_PYCODE,
  TRIVIA_RAW_ENV_LUACODE,
  TRIVIA_RAW_ENV_LUACODE_STAR,
  TRIVIA_RAW_ENV_SAGESILENT,
  TRIVIA_RAW_ENV_SAGEBLOCK,
};

static bool find_verbatim(Lexer *lexer, const char *keyword,
                          bool is_command_name) {
  bool has_marked = false;
  while (true) {
    if (lexer->eof(lexer)) {
      break;
    }

    bool advanced = false;
    bool failed = false;
    for (size_t i = 0; keyword[i]; i++) {
      if (lexer->eof(lexer)) {
        return has_marked;
      }

      if (lexer->lookahead != keyword[i]) {
        failed = true;
        break;
      }

      lexer->advance(lexer, false);
      advanced = true;
    }

    if (failed && !advanced) {
      lexer->advance(lexer, false);
      lexer->markEnd(lexer);
      has_marked = true;
      continue;
    }

    if (!failed) {
      if (is_command_name) {
        if (lexer->eof(lexer)) {
          return has_marked;
        }

        char c = lexer->lookahead;
        switch (c) {
        case ':':
        case '_':
        case '@':
          failed = true;
          break;
        default:
          failed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
          break;
        }

        if (failed) {
          lexer->markEnd(lexer);
          has_marked = true;
          continue;
        }
      }

      return has_marked;
    }
  }

  return has_marked;
}

void *Create() { return NULL; }

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

static unsigned Serialize(void *payload_,
                                                      char *buffer) {
    VoidPtr payload{payload_};
  return 0;
}

static void Deserialize(void *payload_,
                                                    const char *buffer,
                                                    unsigned length) {
    VoidPtr payload{payload_};}

static bool Scan(void *payload_, Lexer *lexer,
                                             const bool *valid_symbols) {
    VoidPtr payload{payload_};
  bool found = false;
  Symbol type = 0xFFFF;
  for (int i = 0; i <= TRIVIA_RAW_ENV_SAGEBLOCK; i++) {
    if (valid_symbols[i]) {
      if (found) {
        return false;
      } else {
        found = true;
        type = i;
      }
    }
  }

  lexer->resultSymbol = type;
  switch (type) {
  case TRIVIA_RAW_FI:
    return find_verbatim(lexer, "\\fi", true);
  case TRIVIA_RAW_ENV_COMMENT:
    return find_verbatim(lexer, "\\end{comment}", false);
  case TRIVIA_RAW_ENV_VERBATIM:
    return find_verbatim(lexer, "\\end{verbatim}", false);
  case TRIVIA_RAW_ENV_LISTING:
    return find_verbatim(lexer, "\\end{lstlisting}", false);
  case TRIVIA_RAW_ENV_MINTED:
    return find_verbatim(lexer, "\\end{minted}", false);
  case TRIVIA_RAW_ENV_PYCODE:
    return find_verbatim(lexer, "\\end{pycode}", false);
  case TRIVIA_RAW_ENV_ASY:
    return find_verbatim(lexer, "\\end{asy}", false);
  case TRIVIA_RAW_ENV_ASYDEF:
    return find_verbatim(lexer, "\\end{asydef}", false);
  case TRIVIA_RAW_ENV_LUACODE:
    return find_verbatim(lexer, "\\end{luacode}", false);
  case TRIVIA_RAW_ENV_LUACODE_STAR:
    return find_verbatim(lexer, "\\end{luacode*}", false);
  case TRIVIA_RAW_ENV_SAGESILENT:
    return find_verbatim(lexer, "\\end{sagesilent}", false);
  case TRIVIA_RAW_ENV_SAGEBLOCK:
    return find_verbatim(lexer, "\\end{sageblock}", false);
  }

  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::latex
