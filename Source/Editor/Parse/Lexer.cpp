#include "Editor/Parse/Lexer.h"

#include <cstring>

namespace ned::editor::parse {

namespace {

    constexpr std::int32_t kByteOrderMark = 0xFEFF;

    constexpr Range kDefaultRange = {
        .startPoint = {.row = 0, .column = 0},
        .endPoint   = {.row = 0xFFFFFFFF, .column = 0xFFFFFFFF},
        .startByte  = 0,
        .endByte    = 0xFFFFFFFF,
    };

    // Strict UTF-8 decode of one codepoint (ICU U8_NEXT semantics: overlongs,
    // surrogates, out-of-range and truncated sequences are errors). On error the
    // caller forces the consumed size to 1, matching lexer.c.
    std::int32_t DecodeUtf8(const std::uint8_t* bytes, std::uint32_t length, std::uint32_t* size) {
        const std::uint8_t lead = bytes[0];
        if (lead < 0x80) {
            *size = 1;
            return lead;
        }
        std::uint32_t continuations = 0;
        std::int32_t  codepoint     = 0;
        if ((lead & 0xE0) == 0xC0) {
            continuations = 1;
            codepoint     = lead & 0x1F;
        }
        else if ((lead & 0xF0) == 0xE0) {
            continuations = 2;
            codepoint     = lead & 0x0F;
        }
        else if ((lead & 0xF8) == 0xF0) {
            continuations = 3;
            codepoint     = lead & 0x07;
        }
        else {
            *size = 1;
            return kDecodeError;
        }
        if (continuations + 1 > length) {
            *size = 1;
            return kDecodeError;
        }
        for (std::uint32_t i = 1; i <= continuations; i++) {
            if ((bytes[i] & 0xC0) != 0x80) {
                *size = 1;
                return kDecodeError;
            }
            codepoint = (codepoint << 6) | (bytes[i] & 0x3F);
        }
        const bool overlong = (continuations == 1 && codepoint < 0x80) || (continuations == 2 && codepoint < 0x800) ||
                              (continuations == 3 && codepoint < 0x10000);
        if (overlong || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            *size = 1;
            return kDecodeError;
        }
        *size = continuations + 1;
        return codepoint;
    }

} // namespace

Lexer::Lexer() {
    data = abi::LexerData{
        .lookahead              = 0,
        .resultSymbol           = 0,
        .advance                = &Lexer::AdvanceCallback,
        .markEnd                = &Lexer::MarkEndCallback,
        .getColumn              = &Lexer::GetColumnCallback,
        .isAtIncludedRangeStart = &Lexer::IsAtIncludedRangeStartCallback,
        .eof                    = &Lexer::EofCallback,
        .log                    = &Lexer::LogCallback,
    };
    currentPosition    = LengthZero();
    tokenStartPosition = LengthZero();
    tokenEndPosition   = kLengthUndefined;
    SetIncludedRanges(nullptr, 0);
}

Lexer::~Lexer() {
    includedRanges.Delete();
}

bool Lexer::AtEof() const {
    return currentIncludedRangeIndex == includedRanges.size;
}

void Lexer::SetText(std::string_view newText) {
    text = newText;
    Goto(currentPosition);
}

bool Lexer::SetIncludedRanges(const Range* ranges, std::uint32_t count) {
    if (count == 0 || ranges == nullptr) {
        ranges = &kDefaultRange;
        count  = 1;
    }
    else {
        std::uint32_t previousByte = 0;
        for (std::uint32_t i = 0; i < count; i++) {
            const Range& range = ranges[i];
            if (range.startByte < previousByte || range.endByte < range.startByte)
                return false;
            previousByte = range.endByte;
        }
    }

    includedRanges.Clear();
    includedRanges.Reserve(count);
    std::memcpy(includedRanges.contents, ranges, count * sizeof(Range));
    includedRanges.size = count;
    Goto(currentPosition);
    return true;
}

void Lexer::Goto(Length position) {
    if (position.bytes != currentPosition.bytes)
        columnData = ColumnData{};

    currentPosition = position;

    bool foundIncludedRange = false;
    for (std::uint32_t i = 0; i < includedRanges.size; i++) {
        const Range& includedRange = includedRanges[i];
        if (includedRange.endByte > currentPosition.bytes && includedRange.endByte > includedRange.startByte) {
            if (includedRange.startByte >= currentPosition.bytes)
                currentPosition = Length{includedRange.startByte, includedRange.startPoint};
            currentIncludedRangeIndex = i;
            foundIncludedRange        = true;
            break;
        }
    }

    if (foundIncludedRange) {
        lookaheadSize  = 0;
        data.lookahead = '\0';
    }
    else {
        currentIncludedRangeIndex      = includedRanges.size;
        const Range& lastIncludedRange = includedRanges[includedRanges.size - 1];
        currentPosition                = Length{lastIncludedRange.endByte, lastIncludedRange.endPoint};
        lookaheadSize                  = 1;
        data.lookahead                 = '\0';
    }
}

void Lexer::GetLookahead() {
    const std::uint32_t position  = currentPosition.bytes;
    const std::uint32_t remaining = position < text.size() ? static_cast<std::uint32_t>(text.size()) - position : 0;
    if (remaining == 0) {
        // Upstream reaches end-of-text through ts_lexer__get_chunk returning
        // an empty chunk, which flips the lexer into the EOF state (the
        // included-range index moves past the last range). Generated lexers
        // and external scanners poll eof() for exactly this.
        currentIncludedRangeIndex = includedRanges.size;
        lookaheadSize             = 1;
        data.lookahead            = '\0';
        return;
    }
    data.lookahead = DecodeUtf8(reinterpret_cast<const std::uint8_t*>(text.data()) + position, remaining, &lookaheadSize);
    if (data.lookahead == kDecodeError)
        lookaheadSize = 1;
}

void Lexer::DoAdvance(bool skip) {
    if (lookaheadSize != 0) {
        if (data.lookahead == '\n') {
            currentPosition.extent.row++;
            currentPosition.extent.column = 0;
            columnData                    = ColumnData{.valid = true, .value = 0};
        }
        else {
            const bool isBom = currentPosition.bytes == 0 && data.lookahead == kByteOrderMark;
            if (!isBom && columnData.valid)
                columnData.value++;
            currentPosition.extent.column += lookaheadSize;
        }
        currentPosition.bytes += lookaheadSize;
    }

    const Range* currentRange = nullptr;
    if (currentIncludedRangeIndex < includedRanges.size) {
        currentRange = &includedRanges[currentIncludedRangeIndex];
        while (currentPosition.bytes >= currentRange->endByte || currentRange->endByte == currentRange->startByte) {
            if (currentIncludedRangeIndex < includedRanges.size)
                currentIncludedRangeIndex++;
            if (currentIncludedRangeIndex < includedRanges.size) {
                currentRange++;
                currentPosition = Length{currentRange->startByte, currentRange->startPoint};
            }
            else {
                currentRange = nullptr;
                break;
            }
        }
    }

    if (skip)
        tokenStartPosition = currentPosition;

    if (currentRange != nullptr) {
        GetLookahead();
    }
    else {
        data.lookahead = '\0';
        lookaheadSize  = 1;
    }
}

void Lexer::MarkEnd() {
    if (!AtEof()) {
        // If the lexer is right at the beginning of an included range, the
        // token should be considered to end at the end of the previous
        // included range instead.
        const Range& currentIncludedRange = includedRanges[currentIncludedRangeIndex];
        if (currentIncludedRangeIndex > 0 && currentPosition.bytes == currentIncludedRange.startByte) {
            const Range& previousIncludedRange = includedRanges[currentIncludedRangeIndex - 1];
            tokenEndPosition                   = Length{previousIncludedRange.endByte, previousIncludedRange.endPoint};
            return;
        }
    }
    tokenEndPosition = currentPosition;
}

void Lexer::Reset(Length position) {
    if (position.bytes != currentPosition.bytes)
        Goto(position);
}

void Lexer::Start() {
    tokenStartPosition = currentPosition;
    tokenEndPosition   = kLengthUndefined;
    data.resultSymbol  = 0;
    didGetColumn       = false;
    if (!AtEof()) {
        if (lookaheadSize == 0)
            GetLookahead();
        if (currentPosition.bytes == 0) {
            if (data.lookahead == kByteOrderMark)
                AdvanceCallback(&data, true);
            columnData = ColumnData{.valid = true, .value = 0};
        }
    }
}

void Lexer::Finish(std::uint32_t* lookaheadEndByte) {
    if (LengthIsUndefined(tokenEndPosition))
        MarkEnd();

    // If the token ended at an included range boundary, its end position was
    // reset to the end of the preceding range; match the start position.
    if (tokenEndPosition.bytes < tokenStartPosition.bytes)
        tokenStartPosition = tokenEndPosition;

    std::uint32_t currentLookaheadEndByte = currentPosition.bytes + 1;

    // Identifying an invalid byte sequence may have looked at up to four
    // following bytes.
    if (data.lookahead == kDecodeError)
        currentLookaheadEndByte += 4;

    if (currentLookaheadEndByte > *lookaheadEndByte)
        *lookaheadEndByte = currentLookaheadEndByte;
}

void Lexer::AdvanceCallback(abi::LexerData* data, bool skip) {
    auto* self = reinterpret_cast<Lexer*>(data);
    // Upstream's advance is a no-op once the chunk is gone, which happens
    // exactly when the lexer has entered the EOF state.
    if (self->AtEof())
        return;
    self->DoAdvance(skip);
}

void Lexer::MarkEndCallback(abi::LexerData* data) {
    reinterpret_cast<Lexer*>(data)->MarkEnd();
}

std::uint32_t Lexer::GetColumnCallback(abi::LexerData* data) {
    auto* self         = reinterpret_cast<Lexer*>(data);
    self->didGetColumn = true;

    if (!self->columnData.valid) {
        const std::uint32_t goalByte      = self->currentPosition.bytes;
        const Length        startOfColumn = {
            self->currentPosition.bytes - self->currentPosition.extent.column,
            {self->currentPosition.extent.row, 0},
        };
        self->Goto(startOfColumn);
        self->columnData = ColumnData{.valid = true, .value = 0};

        if (!self->AtEof()) {
            self->GetLookahead();
            while (self->currentPosition.bytes < goalByte && !self->AtEof()) {
                self->DoAdvance(false);
                if (self->AtEof())
                    break;
            }
        }
    }

    return self->columnData.value;
}

bool Lexer::IsAtIncludedRangeStartCallback(const abi::LexerData* data) {
    const auto* self = reinterpret_cast<const Lexer*>(data);
    if (self->currentIncludedRangeIndex < self->includedRanges.size) {
        const Range& currentRange = self->includedRanges[self->currentIncludedRangeIndex];
        return self->currentPosition.bytes == currentRange.startByte;
    }
    return false;
}

bool Lexer::EofCallback(const abi::LexerData* data) {
    return reinterpret_cast<const Lexer*>(data)->AtEof();
}

void Lexer::LogCallback(const abi::LexerData* data, const char* format, ...) {
    (void)data;
    (void)format;
}

} // namespace ned::editor::parse
