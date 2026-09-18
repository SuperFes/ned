// The scanner for Tests/LanguagePackage/demo: what an out-of-tree scanner
// library looks like end to end -- built against Editor/Parse/Scanner.h
// alone and exporting its table under the `ned_scanner_<name>` name.

#include "Editor/Parse/Scanner.h"

namespace {

using namespace ned::editor::parse::scanner;

enum TokenType { LINE_END };

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
    if (!validSymbols[LINE_END])
        return false;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t')
        lexer->advance(lexer, true);
    if (lexer->lookahead != '\n')
        return false;
    lexer->advance(lexer, false);
    lexer->markEnd(lexer);
    lexer->resultSymbol = LINE_END;
    return true;
}

const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace

NED_SCANNER_LIBRARY_EXPORT(demo, kScanner)
