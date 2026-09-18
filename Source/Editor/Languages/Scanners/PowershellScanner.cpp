// The powershell external scanner, ported from https://github.com/airbus-cert/tree-sitter-powershell (src/scanner.c, MIT
// license) to ned's scanner interface. The algorithm and its state are the
// upstream grammar's; only the vocabulary is ned's.

#include "Editor/Parse/Scanner.h"
#include "Editor/Parse/ScannerSupport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace ned::editor::languages::scanners::powershell {

using namespace ned::editor::parse::scanner;

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See the LICENSE file in the project root for full license information.

enum TOKEN_TYPE {
    STATEMENT_TERMINATOR
};

/* --- API --- */

void *Create();

static void Destroy(void *p_);

static unsigned Serialize(void *payload, char *buffer);

static void Deserialize(void *payload, const char *buffer, unsigned length);

static bool Scan(void *payload, Lexer *lexer, const bool *valid_symbols);

/* --- Internal Functions --- */

static void skip(Lexer *lexer) { lexer->advance(lexer, true); }

static bool scan_statement_terminator(void *payload, Lexer *lexer, const bool *valid_symbols)
{
    if (valid_symbols[STATEMENT_TERMINATOR]) {
        lexer->resultSymbol = STATEMENT_TERMINATOR;
        // This token has no characters -- everything is lookahead to determine its existence
        lexer->markEnd(lexer);

        for (;;) {
            if (lexer->lookahead == 0) return true;
            if (lexer->lookahead == '}') return true;
            if (lexer->lookahead == ';') return true;
            if (lexer->lookahead == ')') return true;
            if (lexer->lookahead == '\n') return true;
            if (!iswspace(lexer->lookahead)) return false;
            skip(lexer);
        }
    }

    return false;
}

/* --- API Implementation --- */

static bool Scan(void *payload_, Lexer *lexer, const bool *valid_symbols)
{
    VoidPtr payload{payload_};
    return scan_statement_terminator(payload, lexer, valid_symbols);
}

void *Create()
{
    return NULL;
}

static void Destroy(void *p_)
{
    VoidPtr p{p_};
}

static unsigned Serialize(void *payload_, char *buffer)
{
    VoidPtr payload{payload_};
    return 0;
}

static void Deserialize(void *payload_, const char *buffer, unsigned length)
{
    VoidPtr payload{payload_};
}

extern const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};

} // namespace ned::editor::languages::scanners::powershell
