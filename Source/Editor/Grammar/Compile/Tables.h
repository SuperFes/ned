//
// The tables a grammar compiles to, assembled into the exact layout the
// engine reads (Parse/Abi.h's LanguageData plus Parse/LexDfa.h's DFAs): the
// symbol numbering, public symbol map, metadata, alias sequences, field
// maps, lex modes, reserved words, the large/small parse tables and the
// parse action lists, external scanner states, primary state ids and the
// supertype map -- tree-sitter's `render.rs` with data in place of C.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_TABLES_H
#define NED_EDITOR_GRAMMAR_COMPILE_TABLES_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/LexTable.h"
#include "Editor/Grammar/Compile/NodeTypes.h"
#include "Editor/Grammar/Compile/ParseTable.h"
#include "Editor/Parse/LexDfa.h"

namespace ned::editor::grammar::compile {

// Owns every array LanguageData points into; `Data()` is what a Parser or
// Engine takes.
class CompiledLanguage {
  public:
    [[nodiscard]] const parse::abi::LanguageData* Data() const {
        return &language_->data;
    }

    // Until ned's own scanners exist, a compiled language borrows the
    // external scanner of the generated parser for the same grammar (the
    // external token order is the grammar's, so the two agree).
    void AdoptExternalScanner(const parse::abi::LanguageData& from);

    std::unique_ptr<parse::DfaLanguage>       language_;
    std::vector<std::uint16_t>                parseTable;
    std::vector<std::uint16_t>                smallParseTable;
    std::vector<std::uint32_t>                smallParseTableMap;
    std::vector<parse::abi::ParseActionEntry> parseActions;
    std::vector<std::string>                  symbolNameStorage;
    std::vector<const char*>                  symbolNames;
    std::vector<std::string>                  fieldNameStorage;
    std::vector<const char*>                  fieldNames;
    std::vector<parse::abi::MapSlice>         fieldMapSlices;
    std::vector<parse::abi::FieldMapEntry>    fieldMapEntries;
    std::vector<parse::abi::SymbolMetadata>   symbolMetadata;
    std::vector<parse::abi::Symbol>           publicSymbolMap;
    std::vector<std::uint16_t>                aliasMap;
    std::vector<parse::abi::Symbol>           aliasSequences;
    std::vector<parse::abi::LexerMode>        lexModes;
    std::vector<parse::abi::Symbol>           externalScannerSymbolMap;
    std::vector<parse::abi::StateId>          primaryStateIds;
    std::string                               name;
    std::vector<parse::abi::Symbol>           reservedWords;
    std::vector<parse::abi::Symbol>           supertypeSymbols;
    std::vector<parse::abi::MapSlice>         supertypeMapSlices;
    std::vector<parse::abi::Symbol>           supertypeMapEntries;
    std::unique_ptr<bool[]>                   externalScannerStates;
};

struct AssemblyInput {
    std::string                              name;
    ParseTable                               parseTable;
    LexTables                                lexTables;
    SyntaxGrammar                            syntax;
    LexicalGrammar                           lexical;
    AliasMap                                 defaultAliases;
    std::map<Symbol, std::vector<ChildType>> supertypeSymbolMap;
};

[[nodiscard]] std::unique_ptr<CompiledLanguage> AssembleTables(AssemblyInput input);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_TABLES_H
