#pragma once

#include <bit>
#include <cstdint>

// Ned's own declaration of the tree-sitter ABI-15 data layout.
//
// Every generated parser.c embeds its own copy of these struct definitions
// (each grammar repo vendors src/tree_sitter/parser.h), so the layout is a
// compatibility contract between the generator and any runtime — not a
// private detail of the tree-sitter library. Ned's engine interprets the
// generated tables through this contract directly; Tests/AbiLayoutTest.cpp
// static-asserts these declarations against the vendored header while the
// tree-sitter FetchContent checkout still exists.
//
// The names are namespaced copies, not the C names, so this header can
// coexist with <tree_sitter/api.h>'s opaque typedefs in one translation
// unit. `LanguageData` is what an opaque `const TSLanguage*` really points
// at for abi_version >= 15.

namespace ned::editor::parse::abi {

static_assert(std::endian::native == std::endian::little, "the inline-subtree bit tagging below assumes little-endian");

using StateId = std::uint16_t;
using Symbol  = std::uint16_t;
using FieldId = std::uint16_t;

inline constexpr Symbol        kBuiltinSymbolEnd         = 0;
inline constexpr Symbol        kBuiltinSymbolError       = static_cast<Symbol>(-1);
inline constexpr Symbol        kBuiltinSymbolErrorRepeat = kBuiltinSymbolError - 1;
inline constexpr std::uint32_t kSerializationBufferSize  = 1024;

// The bundled grammar set spans generated ABI versions 13-15 (janet-simple
// is 13; roughly half are 14). Version N's struct is a strict prefix of
// N+1's: 14 ends at primaryStateIds, 13 just before it, and 15 appends
// name/reservedWords/supertypes/metadata. Two rules keep mixed versions
// safe, same as upstream: never read a field beyond the language's own
// version's end, and read `lexModes` as the two-field `LexModeOld` element
// type when abiVersion < 15 (the third field was added in 15).
inline constexpr std::uint32_t kMinAbiVersion = 13;
inline constexpr std::uint32_t kMaxAbiVersion = 15;

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

// The TSLexer struct generated lexers and external scanners are handed.
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

// The pre-ABI-15 element type of the lexModes table.
struct LexModeOld {
    std::uint16_t lexState;
    std::uint16_t externalLexState;
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
    bool (*lexFn)(LexerData*, StateId);
    bool (*keywordLexFn)(LexerData*, StateId);
    Symbol keywordCaptureToken;
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
