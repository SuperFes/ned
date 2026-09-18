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

// While the generated parser.c files are still linked, each one references
// its scanner by tree-sitter's five C symbol names; this exports a ported
// scanner's table under those names. Goes with parser.c.
#define NED_TREE_SITTER_SCANNER_EXPORTS(name, ns)                                                                           \
    extern "C" void* tree_sitter_##name##_external_scanner_create() {                                                       \
        return ns::kScanner.create();                                                                                       \
    }                                                                                                                       \
    extern "C" void tree_sitter_##name##_external_scanner_destroy(void* payload) {                                          \
        ns::kScanner.destroy(payload);                                                                                      \
    }                                                                                                                       \
    extern "C" bool tree_sitter_##name##_external_scanner_scan(void* payload, ned::editor::parse::abi::LexerData* lexer,    \
                                                               const bool* validSymbols) {                                  \
        return ns::kScanner.scan(payload, lexer, validSymbols);                                                             \
    }                                                                                                                       \
    extern "C" unsigned tree_sitter_##name##_external_scanner_serialize(void* payload, char* buffer) {                      \
        return ns::kScanner.serialize(payload, buffer);                                                                     \
    }                                                                                                                       \
    extern "C" void tree_sitter_##name##_external_scanner_deserialize(void* payload, const char* buffer, unsigned length) { \
        ns::kScanner.deserialize(payload, buffer, length);                                                                  \
    }
