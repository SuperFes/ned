#include "TableFile.h"

#include <cstring>

namespace ned::editor::grammar::compile {

namespace {

    using parse::abi::ParseActionEntry;

    class Writer {
      public:
        std::string bytes;

        void U8(std::uint8_t v) {
            bytes.push_back(static_cast<char>(v));
        }
        void U16(std::uint16_t v) {
            U8(static_cast<std::uint8_t>(v));
            U8(static_cast<std::uint8_t>(v >> 8));
        }
        void U32(std::uint32_t v) {
            U16(static_cast<std::uint16_t>(v));
            U16(static_cast<std::uint16_t>(v >> 16));
        }
        void I32(std::int32_t v) {
            U32(static_cast<std::uint32_t>(v));
        }
        void Size(std::size_t v) {
            U32(static_cast<std::uint32_t>(v));
        }
        void Str(std::string_view s) {
            Size(s.size());
            bytes.append(s);
        }
        void U16s(const std::vector<std::uint16_t>& v) {
            Size(v.size());
            for (const std::uint16_t x : v)
                U16(x);
        }
        void U32s(const std::vector<std::uint32_t>& v) {
            Size(v.size());
            for (const std::uint32_t x : v)
                U32(x);
        }
        void Strs(const std::vector<std::string>& v) {
            Size(v.size());
            for (const std::string& s : v)
                Str(s);
        }
        void Slices(const std::vector<parse::abi::MapSlice>& v) {
            Size(v.size());
            for (const parse::abi::MapSlice& s : v) {
                U16(s.index);
                U16(s.length);
            }
        }
    };

    class Reader {
      public:
        explicit Reader(std::string_view bytes) : bytes_(bytes) {
        }

        [[nodiscard]] bool AtEnd() const {
            return pos_ == bytes_.size();
        }
        std::uint8_t U8() {
            if (pos_ >= bytes_.size())
                throw CompileError("tables: truncated");
            return static_cast<std::uint8_t>(bytes_[pos_++]);
        }
        std::uint16_t U16() {
            const std::uint16_t low = U8();
            return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(U8()) << 8));
        }
        std::uint32_t U32() {
            const std::uint32_t low = U16();
            return low | (static_cast<std::uint32_t>(U16()) << 16);
        }
        std::int32_t I32() {
            return static_cast<std::int32_t>(U32());
        }
        // Every element costs at least a byte, so a count past the bytes
        // left is corrupt rather than merely large.
        std::size_t Size() {
            const std::uint32_t n = U32();
            if (n > bytes_.size() - pos_)
                throw CompileError("tables: element count out of range");
            return n;
        }
        std::string Str() {
            const std::size_t n = Size();
            if (n > bytes_.size() - pos_)
                throw CompileError("tables: truncated");
            std::string s(bytes_.substr(pos_, n));
            pos_ += n;
            return s;
        }
        std::vector<std::uint16_t> U16s() {
            std::vector<std::uint16_t> v(Size());
            for (std::uint16_t& x : v)
                x = U16();
            return v;
        }
        std::vector<std::uint32_t> U32s() {
            std::vector<std::uint32_t> v(Size());
            for (std::uint32_t& x : v)
                x = U32();
            return v;
        }
        std::vector<std::string> Strs() {
            std::vector<std::string> v(Size());
            for (std::string& s : v)
                s = Str();
            return v;
        }
        std::vector<parse::abi::MapSlice> Slices() {
            std::vector<parse::abi::MapSlice> v(Size());
            for (parse::abi::MapSlice& s : v) {
                s.index  = U16();
                s.length = U16();
            }
            return v;
        }

      private:
        std::string_view bytes_;
        std::size_t      pos_ = 0;
    };

    void WriteClauses(Writer& w, const std::vector<parse::LexClause>& clauses) {
        w.Size(clauses.size());
        for (const parse::LexClause& c : clauses) {
            w.U8(static_cast<std::uint8_t>(c.kind));
            w.I32(c.start);
            w.I32(c.end);
        }
    }

    std::vector<parse::LexClause> ReadClauses(Reader& r) {
        std::vector<parse::LexClause> clauses(r.Size());
        for (parse::LexClause& c : clauses) {
            const std::uint8_t kind = r.U8();
            if (kind > static_cast<std::uint8_t>(parse::LexClause::Kind::Greater))
                throw CompileError("tables: unknown lex clause kind");
            c.kind  = static_cast<parse::LexClause::Kind>(kind);
            c.start = r.I32();
            c.end   = r.I32();
        }
        return clauses;
    }

    void WriteDfa(Writer& w, const parse::LexDfa& dfa) {
        w.Size(dfa.states.size());
        for (const parse::LexDfaState& s : dfa.states) {
            w.U8(s.hasAccept);
            w.U16(s.acceptSymbol);
            w.U8(s.hasEof);
            w.U16(s.eofState);
            w.Size(s.transitions.size());
            for (const parse::LexDfaTransition& t : s.transitions) {
                w.U16(t.nextState);
                w.U8(t.inMainToken);
                w.U8(t.conditional);
                w.U8(t.largeSet.has_value());
                w.U32(t.largeSet.value_or(0));
                w.U8(t.largeSetCheckEof);
                w.U8(t.assertedAnyOf);
                WriteClauses(w, t.asserted);
                WriteClauses(w, t.negated);
            }
        }
    }

    parse::LexDfa ReadDfa(Reader& r, std::size_t largeSetCount) {
        parse::LexDfa dfa;
        dfa.states.resize(r.Size());
        for (parse::LexDfaState& s : dfa.states) {
            s.hasAccept    = r.U8() != 0;
            s.acceptSymbol = r.U16();
            s.hasEof       = r.U8() != 0;
            s.eofState     = r.U16();
            s.transitions.resize(r.Size());
            for (parse::LexDfaTransition& t : s.transitions) {
                t.nextState         = r.U16();
                t.inMainToken       = r.U8() != 0;
                t.conditional       = r.U8() != 0;
                const bool hasLarge = r.U8() != 0;
                const auto largeSet = r.U32();
                t.largeSetCheckEof  = r.U8() != 0;
                t.assertedAnyOf     = r.U8() != 0;
                t.asserted          = ReadClauses(r);
                t.negated           = ReadClauses(r);
                if (hasLarge) {
                    if (largeSet >= largeSetCount)
                        throw CompileError("tables: large character set index out of range");
                    t.largeSet = largeSet;
                }
            }
        }
        for (const parse::LexDfaState& s : dfa.states) {
            if (s.hasEof && s.eofState >= dfa.states.size())
                throw CompileError("tables: lex state index out of range");
            for (const parse::LexDfaTransition& t : s.transitions)
                if (t.nextState >= dfa.states.size())
                    throw CompileError("tables: lex state index out of range");
        }
        return dfa;
    }

    void WriteActions(Writer& w, const std::vector<ParseActionEntry>& actions) {
        w.Size(actions.size());
        for (std::size_t i = 0; i < actions.size();) {
            const ParseActionEntry& header = actions[i];
            w.U8(header.entry.count);
            w.U8(header.entry.reusable);
            const std::size_t count = header.entry.count;
            if (i + 1 + count > actions.size())
                throw CompileError("tables: parse action list overruns its array");
            for (std::size_t k = 1; k <= count; ++k) {
                const ParseActionEntry& cell = actions[i + k];
                w.U8(cell.action.type);
                switch (cell.action.type) {
                    case parse::abi::ParseActionTypeShift:
                        w.U16(cell.action.shift.state);
                        w.U8(cell.action.shift.extra);
                        w.U8(cell.action.shift.repetition);
                        break;
                    case parse::abi::ParseActionTypeReduce:
                        w.U8(cell.action.reduce.childCount);
                        w.U16(cell.action.reduce.symbol);
                        w.U16(static_cast<std::uint16_t>(cell.action.reduce.dynamicPrecedence));
                        w.U16(cell.action.reduce.productionId);
                        break;
                    default:
                        break;
                }
            }
            i += 1 + count;
        }
    }

    std::vector<ParseActionEntry> ReadActions(Reader& r) {
        const std::size_t             total = r.Size();
        std::vector<ParseActionEntry> actions;
        actions.reserve(total);
        while (actions.size() < total) {
            ParseActionEntry header{};
            header.entry.count    = r.U8();
            header.entry.reusable = r.U8() != 0;
            actions.push_back(header);
            for (std::size_t k = 0; k < header.entry.count; ++k) {
                ParseActionEntry cell{};
                cell.action.type = r.U8();
                switch (cell.action.type) {
                    case parse::abi::ParseActionTypeShift:
                        cell.action.shift.state      = r.U16();
                        cell.action.shift.extra      = r.U8() != 0;
                        cell.action.shift.repetition = r.U8() != 0;
                        break;
                    case parse::abi::ParseActionTypeReduce:
                        cell.action.reduce.childCount        = r.U8();
                        cell.action.reduce.symbol            = r.U16();
                        cell.action.reduce.dynamicPrecedence = static_cast<std::int16_t>(r.U16());
                        cell.action.reduce.productionId      = r.U16();
                        break;
                    case parse::abi::ParseActionTypeAccept:
                    case parse::abi::ParseActionTypeRecover:
                        break;
                    default:
                        throw CompileError("tables: unknown parse action type");
                }
                actions.push_back(cell);
            }
        }
        if (actions.size() != total)
            throw CompileError("tables: parse action list length disagrees with its count");
        return actions;
    }

} // namespace

std::string SerializeLanguage(const CompiledLanguage& language) {
    const parse::abi::LanguageData& data = *language.Data();
    const parse::DfaLanguage&       dfa  = *language.language_;
    Writer                          w;
    w.bytes.append(kTableFileMagic);
    w.U32(kTableFileVersion);
    w.Str(language.name);

    w.U32(data.abiVersion);
    w.U32(data.symbolCount);
    w.U32(data.aliasCount);
    w.U32(data.tokenCount);
    w.U32(data.externalTokenCount);
    w.U32(data.stateCount);
    w.U32(data.largeStateCount);
    w.U32(data.productionIdCount);
    w.U32(data.fieldCount);
    w.U16(data.maxAliasSequenceLength);
    w.U16(data.keywordCaptureToken);
    w.U16(data.maxReservedWordSetSize);
    w.U32(data.supertypeCount);

    w.U16s(language.parseTable);
    w.U16s(language.smallParseTable);
    w.U32s(language.smallParseTableMap);
    WriteActions(w, language.parseActions);
    w.Strs(language.symbolNameStorage);
    w.Strs(language.fieldNameStorage);
    w.Slices(language.fieldMapSlices);
    w.Size(language.fieldMapEntries.size());
    for (const parse::abi::FieldMapEntry& e : language.fieldMapEntries) {
        w.U16(e.fieldId);
        w.U8(e.childIndex);
        w.U8(e.inherited);
    }
    w.Size(language.symbolMetadata.size());
    for (const parse::abi::SymbolMetadata& m : language.symbolMetadata) {
        w.U8(m.visible);
        w.U8(m.named);
        w.U8(m.supertype);
    }
    w.U16s(language.publicSymbolMap);
    w.U16s(language.aliasMap);
    w.U16s(language.aliasSequences);
    w.Size(language.lexModes.size());
    for (const parse::abi::LexerMode& m : language.lexModes) {
        w.U16(m.lexState);
        w.U16(m.externalLexState);
        w.U16(m.reservedWordSetId);
    }
    w.U16s(language.externalScannerSymbolMap);
    w.Size(language.externalScannerStateCount);
    for (std::size_t i = 0; i < language.externalScannerStateCount * data.externalTokenCount; ++i)
        w.U8(language.externalScannerStates[i]);
    w.U16s(language.primaryStateIds);
    w.U16s(language.reservedWords);
    w.U16s(language.supertypeSymbols);
    w.Slices(language.supertypeMapSlices);
    w.U16s(language.supertypeMapEntries);

    w.Size(dfa.largeCharacterSets.size());
    for (const std::vector<parse::LexCharacterRange>& set : dfa.largeCharacterSets) {
        w.Size(set.size());
        for (const parse::LexCharacterRange& range : set) {
            w.I32(range.start);
            w.I32(range.end);
        }
    }
    WriteDfa(w, dfa.main);
    WriteDfa(w, dfa.keyword);
    return std::move(w.bytes);
}

std::unique_ptr<CompiledLanguage> LoadLanguage(std::string_view bytes) {
    if (bytes.size() < kTableFileMagic.size() || bytes.substr(0, kTableFileMagic.size()) != kTableFileMagic)
        throw CompileError("tables: not a ned table file");
    Reader r(bytes.substr(kTableFileMagic.size()));
    if (const std::uint32_t version = r.U32(); version != kTableFileVersion)
        throw CompileError("tables: format version " + std::to_string(version) + ", this ned reads version " + std::to_string(kTableFileVersion));

    auto language       = std::make_unique<CompiledLanguage>();
    language->language_ = std::make_unique<parse::DfaLanguage>();
    language->name      = r.Str();

    parse::abi::LanguageData& data = language->language_->data;
    data.abiVersion                = r.U32();
    if (data.abiVersion != parse::abi::kAbiVersion)
        throw CompileError("tables: language tables version " + std::to_string(data.abiVersion) + ", this ned reads " + std::to_string(parse::abi::kAbiVersion));
    data.symbolCount               = r.U32();
    data.aliasCount                = r.U32();
    data.tokenCount                = r.U32();
    data.externalTokenCount        = r.U32();
    data.stateCount                = r.U32();
    data.largeStateCount           = r.U32();
    data.productionIdCount         = r.U32();
    data.fieldCount                = r.U32();
    data.maxAliasSequenceLength    = r.U16();
    data.keywordCaptureToken       = r.U16();
    data.maxReservedWordSetSize    = r.U16();
    data.supertypeCount            = r.U32();

    language->parseTable         = r.U16s();
    language->smallParseTable    = r.U16s();
    language->smallParseTableMap = r.U32s();
    language->parseActions       = ReadActions(r);
    language->symbolNameStorage  = r.Strs();
    language->fieldNameStorage   = r.Strs();
    language->fieldMapSlices     = r.Slices();
    language->fieldMapEntries.resize(r.Size());
    for (parse::abi::FieldMapEntry& e : language->fieldMapEntries) {
        e.fieldId    = r.U16();
        e.childIndex = r.U8();
        e.inherited  = r.U8() != 0;
    }
    language->symbolMetadata.resize(r.Size());
    for (parse::abi::SymbolMetadata& m : language->symbolMetadata) {
        m.visible   = r.U8() != 0;
        m.named     = r.U8() != 0;
        m.supertype = r.U8() != 0;
    }
    language->publicSymbolMap = r.U16s();
    language->aliasMap        = r.U16s();
    language->aliasSequences  = r.U16s();
    language->lexModes.resize(r.Size());
    for (parse::abi::LexerMode& m : language->lexModes) {
        m.lexState          = r.U16();
        m.externalLexState  = r.U16();
        m.reservedWordSetId = r.U16();
    }
    language->externalScannerSymbolMap  = r.U16s();
    language->externalScannerStateCount = r.Size();
    const std::size_t stateCells        = language->externalScannerStateCount * data.externalTokenCount;
    language->externalScannerStates     = std::make_unique<bool[]>(stateCells);
    for (std::size_t i = 0; i < stateCells; ++i)
        language->externalScannerStates[i] = r.U8() != 0;
    language->primaryStateIds     = r.U16s();
    language->reservedWords       = r.U16s();
    language->supertypeSymbols    = r.U16s();
    language->supertypeMapSlices  = r.Slices();
    language->supertypeMapEntries = r.U16s();

    parse::DfaLanguage& dfa = *language->language_;
    dfa.largeCharacterSets.resize(r.Size());
    for (std::vector<parse::LexCharacterRange>& set : dfa.largeCharacterSets) {
        set.resize(r.Size());
        for (parse::LexCharacterRange& range : set) {
            range.start = r.I32();
            range.end   = r.I32();
        }
    }
    dfa.main    = ReadDfa(r, dfa.largeCharacterSets.size());
    dfa.keyword = ReadDfa(r, dfa.largeCharacterSets.size());
    if (!r.AtEnd())
        throw CompileError("tables: trailing bytes");

    // The counts the engine indexes by must agree with what was stored.
    if (language->symbolNameStorage.size() != data.symbolCount + data.aliasCount || language->symbolMetadata.size() != language->symbolNameStorage.size() ||
        language->publicSymbolMap.size() != language->symbolNameStorage.size() || language->lexModes.size() != data.stateCount ||
        language->primaryStateIds.size() != data.stateCount || language->parseTable.size() != static_cast<std::size_t>(data.largeStateCount) * data.symbolCount ||
        language->fieldNameStorage.size() != data.fieldCount + 1 || language->supertypeSymbols.size() != data.supertypeCount ||
        language->externalScannerSymbolMap.size() != data.externalTokenCount ||
        (data.stateCount > data.largeStateCount && language->smallParseTableMap.size() != data.stateCount - data.largeStateCount))
        throw CompileError("tables: table sizes disagree with the header counts");

    language->Link();
    return language;
}

} // namespace ned::editor::grammar::compile
