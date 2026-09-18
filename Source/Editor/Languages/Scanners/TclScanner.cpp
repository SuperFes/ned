// The tcl external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-tcl (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::tcl {

using namespace ned::editor::parse::scanner;

enum TokenType {
  CONCAT,
  IMMEDIATE
};

void *Create() {
  return NULL;
}

static bool Scan(void *payload_, Lexer *lexer,
                                          const bool *valid_symbols) {
    VoidPtr payload{payload_};
  int32_t c = lexer->lookahead;

  if (valid_symbols[IMMEDIATE] && !iswspace(c)) {
    lexer->resultSymbol = IMMEDIATE;
    return false;
  }

  if (valid_symbols[CONCAT] && (
        !iswspace(c) &&
        c != ')' &&
        c != ':' &&
        c != '}' &&
        c != ']')) {
    lexer->resultSymbol = CONCAT;
    return true;
  }

  return false;
}

static unsigned Serialize(void *payload_, char *state) {
    VoidPtr payload{payload_};
  return 0;
}

static void Deserialize(void *payload_, const char *state, unsigned length){
    VoidPtr payload{payload_}; }

static void Destroy(void *payload_) {
    VoidPtr payload{payload_};}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::tcl
