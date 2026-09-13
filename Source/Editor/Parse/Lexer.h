#pragma once

#include <string_view>

#include "Editor/Parse/Abi.h"
#include "Editor/Parse/Green.h"
#include "Editor/Parse/RawArray.h"

// The TSLexer implementation generated lexers and external scanners are
// driven through — the port of tree-sitter's lexer.c over a contiguous
// buffer. `data` must be the first member: lex_fn and external scanners
// receive `&data` and the callbacks cast back to the enclosing Lexer.

namespace ned::editor::parse {

struct Range {
    abi::Point    startPoint;
    abi::Point    endPoint;
    std::uint32_t startByte;
    std::uint32_t endByte;
};

inline constexpr std::int32_t kDecodeError = -1;

struct ColumnData {
    bool          valid = false;
    std::uint32_t value = 0;
};

struct Lexer {
    abi::LexerData data;
    Length         currentPosition;
    Length         tokenStartPosition;
    Length         tokenEndPosition;

    RawArray<Range> includedRanges;
    std::uint32_t   currentIncludedRangeIndex = 0;

    std::string_view text;
    std::uint32_t    lookaheadSize = 0;
    bool             didGetColumn  = false;
    ColumnData       columnData;

    char scratchBuffer[abi::kSerializationBufferSize];

    Lexer();
    ~Lexer();
    Lexer(const Lexer&)            = delete;
    Lexer& operator=(const Lexer&) = delete;

    void               SetText(std::string_view newText);
    bool               SetIncludedRanges(const Range* ranges, std::uint32_t count);
    void               Reset(Length position);
    void               Start();
    void               Finish(std::uint32_t* lookaheadEndByte);
    void               MarkEnd();
    [[nodiscard]] bool AtEof() const;

  private:
    void Goto(Length position);
    void GetLookahead();
    void DoAdvance(bool skip);

    static void          AdvanceCallback(abi::LexerData* data, bool skip);
    static void          MarkEndCallback(abi::LexerData* data);
    static std::uint32_t GetColumnCallback(abi::LexerData* data);
    static bool          IsAtIncludedRangeStartCallback(const abi::LexerData* data);
    static bool          EofCallback(const abi::LexerData* data);
    static void          LogCallback(const abi::LexerData* data, const char* format, ...);
};

} // namespace ned::editor::parse
