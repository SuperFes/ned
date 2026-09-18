#pragma once

#include <cstdint>

#include "Editor/Parse/Abi.h"

// Read-side accessors over a generated grammar's ABI-15 tables — the port of
// tree-sitter's language.h/language.c helpers. Pure lookups, no state.

namespace ned::editor::parse {

inline constexpr abi::StateId kErrorState = 0;

struct TableEntry {
    const abi::ParseAction* actions;
    std::uint32_t           actionCount;
    bool                    isReusable;
};

// For terminal symbols the table value indexes the actions array; for
// non-terminals it is the successor state. Large states are direct rows,
// small states are searched section lists.
inline std::uint16_t LanguageLookup(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol) {
    if (state >= language->largeStateCount) {
        const std::uint32_t  index      = language->smallParseTableMap[state - language->largeStateCount];
        const std::uint16_t* data       = &language->smallParseTable[index];
        const std::uint16_t  groupCount = *(data++);
        for (unsigned i = 0; i < groupCount; i++) {
            const std::uint16_t sectionValue = *(data++);
            const std::uint16_t symbolCount  = *(data++);
            for (unsigned j = 0; j < symbolCount; j++) {
                if (*(data++) == symbol)
                    return sectionValue;
            }
        }
        return 0;
    }
    return language->parseTable[state * language->symbolCount + symbol];
}

inline void LanguageTableEntry(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol, TableEntry* result) {
    if (symbol == abi::kBuiltinSymbolError || symbol == abi::kBuiltinSymbolErrorRepeat) {
        result->actionCount = 0;
        result->isReusable  = false;
        result->actions     = nullptr;
        return;
    }
    const std::uint32_t          actionIndex = LanguageLookup(language, state, symbol);
    const abi::ParseActionEntry* entry       = &language->parseActions[actionIndex];
    result->actionCount                      = entry->entry.count;
    result->isReusable                       = entry->entry.reusable;
    result->actions                          = reinterpret_cast<const abi::ParseAction*>(entry + 1);
}

inline const abi::ParseAction* LanguageActions(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol, std::uint32_t* count) {
    TableEntry entry;
    LanguageTableEntry(language, state, symbol, &entry);
    *count = entry.actionCount;
    return entry.actions;
}

inline bool LanguageHasActions(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol) {
    return LanguageLookup(language, state, symbol) != 0;
}

inline bool LanguageHasReduceAction(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol) {
    TableEntry entry;
    LanguageTableEntry(language, state, symbol, &entry);
    return entry.actionCount > 0 && entry.actions[0].type == abi::ParseActionTypeReduce;
}

inline abi::LexerMode LanguageLexModeForState(const abi::LanguageData* language, abi::StateId state) {
    return language->lexModes[state];
}

inline bool LanguageIsReservedWord(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol) {
    const abi::LexerMode lexMode = LanguageLexModeForState(language, state);
    if (lexMode.reservedWordSetId > 0) {
        const unsigned start = lexMode.reservedWordSetId * language->maxReservedWordSetSize;
        const unsigned end   = start + language->maxReservedWordSetSize;
        for (unsigned i = start; i < end; i++) {
            if (language->reservedWords[i] == symbol)
                return true;
            if (language->reservedWords[i] == 0)
                break;
        }
    }
    return false;
}

inline abi::SymbolMetadata LanguageSymbolMetadata(const abi::LanguageData* language, abi::Symbol symbol) {
    if (symbol == abi::kBuiltinSymbolError)
        return {.visible = true, .named = true, .supertype = false};
    if (symbol == abi::kBuiltinSymbolErrorRepeat)
        return {.visible = false, .named = false, .supertype = false};
    return language->symbolMetadata[symbol];
}

inline abi::StateId LanguageNextState(const abi::LanguageData* language, abi::StateId state, abi::Symbol symbol) {
    if (symbol == abi::kBuiltinSymbolError || symbol == abi::kBuiltinSymbolErrorRepeat)
        return 0;
    if (symbol < language->tokenCount) {
        std::uint32_t           count   = 0;
        const abi::ParseAction* actions = LanguageActions(language, state, symbol, &count);
        if (count > 0) {
            const abi::ParseAction action = actions[count - 1];
            if (action.type == abi::ParseActionTypeShift)
                return action.shift.extra ? state : action.shift.state;
        }
        return 0;
    }
    return LanguageLookup(language, state, symbol);
}

inline const char* LanguageSymbolName(const abi::LanguageData* language, abi::Symbol symbol) {
    if (symbol == abi::kBuiltinSymbolError)
        return "ERROR";
    if (symbol == abi::kBuiltinSymbolErrorRepeat)
        return "_ERROR";
    if (symbol < language->symbolCount + language->aliasCount)
        return language->symbolNames[symbol];
    return nullptr;
}

inline const bool* LanguageEnabledExternalTokens(const abi::LanguageData* language, unsigned externalScannerState) {
    if (externalScannerState == 0)
        return nullptr;
    return language->externalScanner.states + language->externalTokenCount * externalScannerState;
}

inline const abi::Symbol* LanguageAliasSequence(const abi::LanguageData* language, std::uint32_t productionId) {
    return productionId != 0 ? &language->aliasSequences[productionId * language->maxAliasSequenceLength] : nullptr;
}

enum class SymbolType : std::uint8_t {
    Regular,
    Anonymous,
    Supertype,
    Auxiliary,
};

inline std::uint32_t LanguageSymbolCount(const abi::LanguageData* language) {
    return language->symbolCount + language->aliasCount;
}

inline SymbolType LanguageSymbolType(const abi::LanguageData* language, abi::Symbol symbol) {
    const abi::SymbolMetadata metadata = LanguageSymbolMetadata(language, symbol);
    if (metadata.named && metadata.visible)
        return SymbolType::Regular;
    if (metadata.visible)
        return SymbolType::Anonymous;
    if (metadata.supertype)
        return SymbolType::Supertype;
    return SymbolType::Auxiliary;
}

inline const abi::Symbol* LanguageSupertypes(const abi::LanguageData* language, std::uint32_t* length) {
    *length = language->supertypeCount;
    return language->supertypeSymbols;
}

inline const abi::Symbol* LanguageSubtypes(const abi::LanguageData* language, abi::Symbol supertype, std::uint32_t* length) {
    if (!LanguageSymbolMetadata(language, supertype).supertype) {
        *length = 0;
        return nullptr;
    }
    const abi::MapSlice slice = language->supertypeMapSlices[supertype];
    *length                   = slice.length;
    return &language->supertypeMapEntries[slice.index];
}

inline abi::FieldId LanguageFieldIdForName(const abi::LanguageData* language, const char* name, std::uint32_t nameLength) {
    const auto count = static_cast<std::uint16_t>(language->fieldCount);
    for (abi::Symbol i = 1; i < count + 1; i++) {
        const char*   fieldName = language->fieldNames[i];
        std::uint32_t j         = 0;
        while (j < nameLength && fieldName[j] != '\0' && fieldName[j] == name[j])
            j++;
        if (j == nameLength && fieldName[j] == '\0')
            return static_cast<abi::FieldId>(i);
    }
    return 0;
}

inline void LanguageFieldMap(const abi::LanguageData* language, std::uint32_t productionId, const abi::FieldMapEntry** start, const abi::FieldMapEntry** end) {
    if (language->fieldCount == 0) {
        *start = nullptr;
        *end   = nullptr;
        return;
    }
    const abi::MapSlice slice = language->fieldMapSlices[productionId];
    *start                    = &language->fieldMapEntries[slice.index];
    *end                      = &language->fieldMapEntries[slice.index] + slice.length;
}

} // namespace ned::editor::parse
