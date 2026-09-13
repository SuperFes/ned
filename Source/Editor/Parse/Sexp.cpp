#include "Editor/Parse/Sexp.h"

#include <cctype>

#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    void WriteCharToString(std::string& out, std::int32_t chr) {
        if (chr == -1) {
            out += "INVALID";
        }
        else if (chr == '\0') {
            out += "'\\0'";
        }
        else if (chr == '\n') {
            out += "'\\n'";
        }
        else if (chr == '\t') {
            out += "'\\t'";
        }
        else if (chr == '\r') {
            out += "'\\r'";
        }
        else if (0 < chr && chr < 128 && std::isprint(chr) != 0) {
            out += '\'';
            out += static_cast<char>(chr);
            out += '\'';
        }
        else {
            out += std::to_string(chr);
        }
    }

    constexpr const char* kRootField = "__ROOT__";

    void WriteToString(std::string& out, Subtree self, const abi::LanguageData* language, abi::Symbol aliasSymbol,
                       bool aliasIsNamed, const char* fieldName) {
        if (self.ptr == nullptr) {
            out += "(NULL)";
            return;
        }

        const bool isRoot = fieldName == kRootField;
        const bool isVisible =
            SubtreeMissing(self) || (aliasSymbol != 0 ? aliasIsNamed : SubtreeVisible(self) && SubtreeNamed(self));

        if (isVisible) {
            if (!isRoot) {
                out += ' ';
                if (fieldName != nullptr) {
                    out += fieldName;
                    out += ": ";
                }
            }

            if (SubtreeIsError(self) && SubtreeChildCount(self) == 0 && self.ptr->size.bytes > 0) {
                out += "(UNEXPECTED ";
                WriteCharToString(out, self.ptr->lookaheadChar);
            }
            else {
                const abi::Symbol symbol     = aliasSymbol != 0 ? aliasSymbol : SubtreeSymbol(self);
                const char*       symbolName = LanguageSymbolName(language, symbol);
                if (SubtreeMissing(self)) {
                    out += "(MISSING ";
                    if (aliasIsNamed || SubtreeNamed(self)) {
                        out += symbolName;
                    }
                    else {
                        out += '"';
                        out += symbolName;
                        out += '"';
                    }
                }
                else {
                    out += '(';
                    out += symbolName;
                }
            }
        }
        else if (isRoot) {
            const abi::Symbol symbol     = aliasSymbol != 0 ? aliasSymbol : SubtreeSymbol(self);
            const char*       symbolName = LanguageSymbolName(language, symbol);
            if (SubtreeChildCount(self) > 0) {
                out += '(';
                out += symbolName;
            }
            else if (SubtreeNamed(self)) {
                out += '(';
                out += symbolName;
                out += ')';
            }
            else {
                out += "(\"";
                out += symbolName;
                out += "\")";
            }
        }

        if (SubtreeChildCount(self) > 0) {
            const abi::Symbol*        aliasSequence = LanguageAliasSequence(language, self.ptr->productionId);
            const abi::FieldMapEntry* fieldMap      = nullptr;
            const abi::FieldMapEntry* fieldMapEnd   = nullptr;
            LanguageFieldMap(language, self.ptr->productionId, &fieldMap, &fieldMapEnd);

            std::uint32_t structuralChildIndex = 0;
            for (std::uint32_t i = 0; i < self.ptr->childCount; i++) {
                const Subtree child = SubtreeChildren(self)[i];
                if (SubtreeExtra(child)) {
                    WriteToString(out, child, language, 0, false, nullptr);
                }
                else {
                    const abi::Symbol subtreeAliasSymbol =
                        aliasSequence != nullptr ? aliasSequence[structuralChildIndex] : 0;
                    const bool subtreeAliasIsNamed =
                        subtreeAliasSymbol != 0 ? LanguageSymbolMetadata(language, subtreeAliasSymbol).named : false;

                    const char* childFieldName = isVisible ? nullptr : fieldName;
                    for (const abi::FieldMapEntry* map = fieldMap; map < fieldMapEnd; map++) {
                        if (!map->inherited && map->childIndex == structuralChildIndex) {
                            childFieldName = language->fieldNames[map->fieldId];
                            break;
                        }
                    }

                    WriteToString(out, child, language, subtreeAliasSymbol, subtreeAliasIsNamed, childFieldName);
                    structuralChildIndex++;
                }
            }
        }

        if (isVisible)
            out += ')';
    }

} // namespace

std::string SubtreeToSexp(Subtree self, const abi::LanguageData* language) {
    std::string out;
    WriteToString(out, self, language, 0, false, kRootField);
    return out;
}

} // namespace ned::editor::parse
