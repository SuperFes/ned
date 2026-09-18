// The gleam external scanner, ported from https://github.com/gleam-lang/tree-sitter-gleam (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::gleam {

using namespace ned::editor::parse::scanner;

enum TokenType {
  QUOTED_CONTENT,
  DOC_COMMENT_CONTENT,
};

static void * Create() {return NULL;}
static void Destroy(void * payload_) {
    VoidPtr payload{payload_};}
static unsigned Serialize(void * payload_, char * buffer) {
    VoidPtr payload{payload_};return 0;}
static void Deserialize(void * payload_, const char * buffer, unsigned length) {
    VoidPtr payload{payload_};}

static bool Scan(void * payload_, Lexer *lexer, const bool * valid_symbols) {
    VoidPtr payload{payload_};
  if (valid_symbols[QUOTED_CONTENT]) {
    bool has_content = false;

    while (true) {
      if (lexer->lookahead == '\"' || lexer->lookahead == '\\') {
        break;
      } else if (lexer->lookahead == 0) {
        return false;
      }
      has_content = true;
      lexer->advance(lexer, false);
    }
    lexer->resultSymbol = QUOTED_CONTENT;
    return has_content;
  }

  if (valid_symbols[DOC_COMMENT_CONTENT]) {
    lexer->resultSymbol = DOC_COMMENT_CONTENT;
    while (true) {
        if (lexer->eof(lexer)) {
            return true;
        }
        if (lexer->lookahead == '\n') {
            // including the line ending in doc
            // comments is necessary for markdown injections
            lexer->advance(lexer, false);
            return true;
        }
        lexer->advance(lexer, false);
    }
  }
  
  return false;
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::gleam
