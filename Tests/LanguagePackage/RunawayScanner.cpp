// The scanner for Tests/LanguagePackage/runaway: at a backslash it returns
// `gap` without consuming anything and without changing its (empty) state,
// so nothing but the engine stops the parser accepting it forever.

#include "Editor/Parse/Scanner.h"

namespace {

using namespace ned::editor::parse::scanner;

enum TokenType { GAP };

void* Create() {
    return nullptr;
}

void Destroy(void*) {
}

unsigned Serialize(void*, char*) {
    return 0;
}

void Deserialize(void*, const char*, unsigned) {
}

bool Scan(void*, Lexer* lexer, const bool* validSymbols) {
    if (!validSymbols[GAP])
        return false;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n')
        lexer->advance(lexer, true);
    if (lexer->lookahead != '\\')
        return false;
    lexer->markEnd(lexer);
    lexer->resultSymbol = GAP;
    return true;
}

const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace

NED_SCANNER_LIBRARY_EXPORT(runaway, kScanner)
