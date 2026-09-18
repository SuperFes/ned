// The properties external scanner, ported from https://github.com/tree-sitter-grammars/tree-sitter-properties (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ned::editor::languages::scanners::properties {

using namespace ned::editor::parse::scanner;

#ifdef _MSC_VER
#pragma warning(disable : 4100)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

enum TokenType { FAKE_EOL };

static bool reached_eof = false;

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols) {
    VoidPtr payload{payload_};
  lexer->resultSymbol = FAKE_EOL;
  return reached_eof = !reached_eof && valid_symbols[FAKE_EOL] && lexer->eof(lexer);
}

static unsigned Serialize(void *payload_, char *buffer) {
    VoidPtr payload{payload_};
  return reached_eof;
}

static void Deserialize(void *payload_, const char *buffer, unsigned length) {
    VoidPtr payload{payload_};
  reached_eof = length;
}

void *Create() { return NULL; }

static void Destroy(void *payload_) {
    VoidPtr payload{payload_}; }

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::properties
