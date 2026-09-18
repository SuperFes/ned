#pragma once

#include "Editor/Parse/Abi.h"

// The external-scanner interface: what a language's hand-written lexer for
// the tokens no regular expression can express (heredocs, indentation,
// string interpolation) implements. Five functions over the engine's lexer
// (Abi.h's LexerData -- lookahead, advance, markEnd, getColumn, eof), with
// an opaque per-parse state that must serialize into
// kSerializationBufferSize bytes so incremental reparses can resume it.
//
// Bundled scanners live in Editor/Languages/Scanners/ and register a
// ScannerVTable under their grammar's name (Scanners.h); an out-of-tree
// scanner compiles against this header alone and exports the same table.

namespace ned::editor::parse {

struct ScannerVTable {
    void* (*create)();
    void (*destroy)(void* payload);
    bool (*scan)(void* payload, abi::LexerData* lexer, const bool* validSymbols);
    unsigned (*serialize)(void* payload, char* buffer);
    void (*deserialize)(void* payload, const char* buffer, unsigned length);
};

// The vocabulary a scanner is written in.
namespace scanner {
    using Lexer  = abi::LexerData;
    using Symbol = abi::Symbol;

    inline constexpr unsigned kSerializationBufferSize = abi::kSerializationBufferSize;
    inline constexpr Symbol   kEndSymbol               = abi::kBuiltinSymbolEnd;
} // namespace scanner

} // namespace ned::editor::parse

// An out-of-tree scanner is a shared library named by a language
// definition's `:scanner-library`, exporting its table as
// `ned_scanner_<name>` -- this writes that function.
#define NED_SCANNER_LIBRARY_EXPORT(name, table)                                \
    extern "C" const ned::editor::parse::ScannerVTable* ned_scanner_##name() { \
        return &(table);                                                       \
    }
