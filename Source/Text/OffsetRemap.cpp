#include "OffsetRemap.h"

#include <algorithm>
#include <string>

namespace ned::text {

namespace {

    // progressive-huge-file-load follow-up (moved here from Buffer.cpp, where
    // this pair lived when Buffer's restore path was the only consumer): this
    // used to run against two fully-materialized std::strings -- for a
    // multi-GB piece-table buffer that is the exact freeze/OOM the huge-file
    // feature exists to eliminate, and it was not even huge-load-specific:
    // any Undo()/Redo() on any huge buffer paid it.
    //
    // Reading through ITextStorage::Substring in exponentially growing blocks
    // never materializes either side whole, and the total bytes actually read
    // is bounded to O(the common region actually found) by the standard
    // doubling-search argument (a geometric series dominated by its last
    // term). So a single localized edit costs O(edit size) regardless of
    // document size, whether it sits near the start, middle or end.
    std::size_t CommonPrefixLength(const ITextStorage& a, const ITextStorage& b) {
        const std::size_t maxLen    = std::min(a.ByteLength(), b.ByteLength());
        std::size_t       checked   = 0;
        std::size_t       blockSize = 4096;
        while (checked < maxLen) {
            const std::size_t len    = std::min(blockSize, maxLen - checked);
            const std::string blockA = a.Substring(checked, len);
            const std::string blockB = b.Substring(checked, len);
            std::size_t       common = 0;
            while (common < len && blockA[common] == blockB[common]) {
                ++common;
            }
            checked += common;
            if (common < len) {
                return checked;
            }
            blockSize *= 2;
        }
        return maxLen;
    }

    // Mirrors CommonPrefixLength, walking backward from the end of each.
    std::size_t CommonSuffixLength(const ITextStorage& a, const ITextStorage& b, std::size_t maxLen) {
        const std::size_t aLen      = a.ByteLength();
        const std::size_t bLen      = b.ByteLength();
        std::size_t       checked   = 0;
        std::size_t       blockSize = 4096;
        while (checked < maxLen) {
            const std::size_t len    = std::min(blockSize, maxLen - checked);
            const std::string blockA = a.Substring(aLen - checked - len, len);
            const std::string blockB = b.Substring(bLen - checked - len, len);
            std::size_t       common = 0;
            while (common < len && blockA[len - 1 - common] == blockB[len - 1 - common]) {
                ++common;
            }
            checked += common;
            if (common < len) {
                return checked;
            }
            blockSize *= 2;
        }
        return maxLen;
    }

} // namespace

bool StorageContentEquals(const ITextStorage& a, const ITextStorage& b) {
    return a.ByteLength() == b.ByteLength() && CommonPrefixLength(a, b) == a.ByteLength();
}

std::optional<ChangedSpan> ChangedByteRange(const ITextStorage& oldStorage, const ITextStorage& newStorage) {
    const std::size_t oldLen    = oldStorage.ByteLength();
    const std::size_t newLen    = newStorage.ByteLength();
    const std::size_t maxCommon = std::min(oldLen, newLen);

    const std::size_t prefix = CommonPrefixLength(oldStorage, newStorage);
    if (prefix == oldLen && prefix == newLen) {
        return std::nullopt;
    }
    const std::size_t maxSuffix = maxCommon - prefix;
    const std::size_t suffix    = CommonSuffixLength(oldStorage, newStorage, maxSuffix);
    return ChangedSpan{prefix, oldLen - suffix, prefix, newLen - suffix};
}

std::size_t RemapOffset(std::size_t offset, const ChangedSpan& span) {
    if (offset <= span.oldStart) {
        return offset;
    }
    if (offset >= span.oldEnd) {
        return offset - span.oldEnd + span.newEnd;
    }
    // Inside the replaced region: the bytes this named no longer exist. The
    // start of the new span is the only position that is defensible -- see the
    // header for why this does not try to be cleverer.
    return span.newStart;
}

std::size_t RemapOffsetBetween(std::size_t offset, const ITextStorage& oldStorage, const ITextStorage& newStorage) {
    const std::optional<ChangedSpan> span = ChangedByteRange(oldStorage, newStorage);
    return span ? RemapOffset(offset, *span) : offset;
}

} // namespace ned::text
