#pragma once

#include <bit>
#include <cstdint>

// The table layout ned's parser engine runs on: what a language package's
// `tables` file loads into (Grammar/Compile/TableFile.h) and what the
// engine (Parse/Parser.cpp, LanguageTables.h) reads. The generator
// (Grammar/Compile/) produces exactly this; the lexer is the DFA that
// follows it in memory (LexDfa.h's DfaLanguage, whose first member this
// is).
//
// The layout is tree-sitter's ABI-15 language struct minus its lexer
// function pointers, which is what keeps the engine's table-walking code
// a faithful port of the reference; nothing else depends on that.

namespace ned::editor::parse::abi {

static_assert(std::endian::native == std::endian::little, "the inline-subtree bit tagging below assumes little-endian");

using StateId = std::uint16_t;
using Symbol  = std::uint16_t;
using FieldId = std::uint16_t;

inline constexpr Symbol        kBuiltinSymbolEnd         = 0;
inline constexpr Symbol        kBuiltinSymbolError       = static_cast<Symbol>(-1);
inline constexpr Symbol        kBuiltinSymbolErrorRepeat = kBuiltinSymbolError - 1;
inline constexpr std::uint32_t kSerializationBufferSize  = 1024;

// The one table version the engine reads; a package records it so a
// mismatch is refused rather than misread.
inline constexpr std::uint32_t kAbiVersion = 15;

struct Point {
    std::uint32_t row;
    std::uint32_t column;
};

struct FieldMapEntry {
    FieldId      fieldId;
    std::uint8_t childIndex;
    bool         inherited;
};

struct MapSlice {
    std::uint16_t index;
    std::uint16_t length;
};

struct SymbolMetadata {
    bool visible;
    bool named;
    bool supertype;
};

// The lexer an external scanner is handed (Parse/Scanner.h).
struct LexerData {
    std::int32_t lookahead;
    Symbol       resultSymbol;
    void (*advance)(LexerData*, bool);
    void (*markEnd)(LexerData*);
    std::uint32_t (*getColumn)(LexerData*);
    bool (*isAtIncludedRangeStart)(const LexerData*);
    bool (*eof)(const LexerData*);
    void (*log)(const LexerData*, const char*, ...);
};

enum ParseActionType : std::uint8_t {
    ParseActionTypeShift   = 0,
    ParseActionTypeReduce  = 1,
    ParseActionTypeAccept  = 2,
    ParseActionTypeRecover = 3,
};

union ParseAction {
    struct {
        std::uint8_t type;
        StateId      state;
        bool         extra;
        bool         repetition;
    } shift;
    struct {
        std::uint8_t  type;
        std::uint8_t  childCount;
        Symbol        symbol;
        std::int16_t  dynamicPrecedence;
        std::uint16_t productionId;
    } reduce;
    std::uint8_t type;
};

struct LexerMode {
    std::uint16_t lexState;
    std::uint16_t externalLexState;
    std::uint16_t reservedWordSetId;
};

union ParseActionEntry {
    ParseAction action;
    struct {
        std::uint8_t count;
        bool         reusable;
    } entry;
};

struct LanguageMetadata {
    std::uint8_t majorVersion;
    std::uint8_t minorVersion;
    std::uint8_t patchVersion;
};

struct LanguageData {
    std::uint32_t           abiVersion;
    std::uint32_t           symbolCount;
    std::uint32_t           aliasCount;
    std::uint32_t           tokenCount;
    std::uint32_t           externalTokenCount;
    std::uint32_t           stateCount;
    std::uint32_t           largeStateCount;
    std::uint32_t           productionIdCount;
    std::uint32_t           fieldCount;
    std::uint16_t           maxAliasSequenceLength;
    const std::uint16_t*    parseTable;
    const std::uint16_t*    smallParseTable;
    const std::uint32_t*    smallParseTableMap;
    const ParseActionEntry* parseActions;
    const char* const*      symbolNames;
    const char* const*      fieldNames;
    const MapSlice*         fieldMapSlices;
    const FieldMapEntry*    fieldMapEntries;
    const SymbolMetadata*   symbolMetadata;
    const Symbol*           publicSymbolMap;
    const std::uint16_t*    aliasMap;
    const Symbol*           aliasSequences;
    const LexerMode*        lexModes;
    Symbol                  keywordCaptureToken;
    struct {
        const bool*   states;
        const Symbol* symbolMap;
        void* (*create)();
        void (*destroy)(void*);
        bool (*scan)(void*, LexerData*, const bool*);
        unsigned (*serialize)(void*, char*);
        void (*deserialize)(void*, const char*, unsigned);
    } externalScanner;
    const StateId*   primaryStateIds;
    const char*      name;
    const Symbol*    reservedWords;
    std::uint16_t    maxReservedWordSetSize;
    std::uint32_t    supertypeCount;
    const Symbol*    supertypeSymbols;
    const MapSlice*  supertypeMapSlices;
    const Symbol*    supertypeMapEntries;
    LanguageMetadata metadata;
};

} // namespace ned::editor::parse::abi
