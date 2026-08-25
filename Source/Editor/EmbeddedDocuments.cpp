#include "EmbeddedDocuments.h"

#include <algorithm>
#include <unordered_map>

#include "Text/Rope.h"

namespace ned::editor {

namespace {

    // Real Unicode whitespace at every UTF-8 byte width, so a padded region
    // always tokenizes as inert whitespace for whatever language ends up
    // parsing the full document -- see EmbeddedDocuments.h's own doc comment
    // on BuildEmbeddedDocuments for why width 4 is two NBSPs rather than a
    // single astral filler.
    constexpr std::string_view kFiller1 = " ";                // U+0020, 1 byte, UTF-16 width 1
    constexpr std::string_view kFiller2 = "\xC2\xA0";         // U+00A0 NBSP, 2 bytes, UTF-16 width 1
    constexpr std::string_view kFiller3 = "\xE3\x80\x80";     // U+3000 IDEOGRAPHIC SPACE, 3 bytes, UTF-16 width 1
    constexpr std::string_view kFiller4 = "\xC2\xA0\xC2\xA0"; // two NBSPs, 4 bytes, UTF-16 width 2

    std::string_view FillerForByteLength(std::size_t byteLength) {
        switch (byteLength) {
            case 1:
                return kFiller1;
            case 2:
                return kFiller2;
            case 3:
                return kFiller3;
            default:
                return kFiller4; // 4, or (never for real UTF-8) anything larger
        }
    }

    bool ByteInRanges(std::size_t offset, const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
        const auto it = std::upper_bound(ranges.begin(), ranges.end(), offset,
                                         [](std::size_t value, const std::pair<std::size_t, std::size_t>& range) {
                                             return value < range.first;
                                         });
        if (it == ranges.begin()) {
            return false;
        }
        const auto& previous = *std::prev(it);
        return offset >= previous.first && offset < previous.second;
    }

    // Groups regions by language, sorting and merging each language's own
    // ranges -- injection.content captures for one language aren't expected
    // to overlap in practice, but this guards a pathological query rather
    // than assuming.
    std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>>
    GroupRangesByLanguage(const std::vector<InjectionRegion>& regions) {
        std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> byLanguage;
        for (const InjectionRegion& region : regions) {
            byLanguage[region.language].emplace_back(region.startByte, region.endByte);
        }
        for (auto& [language, ranges] : byLanguage) {
            std::sort(ranges.begin(), ranges.end());
            std::vector<std::pair<std::size_t, std::size_t>> merged;
            for (const auto& range : ranges) {
                if (!merged.empty() && range.first <= merged.back().second) {
                    merged.back().second = std::max(merged.back().second, range.second);
                }
                else {
                    merged.push_back(range);
                }
            }
            ranges = std::move(merged);
        }
        return byLanguage;
    }

} // namespace

std::vector<EmbeddedDocument> BuildEmbeddedDocuments(const Mode& mode, std::string_view bufferText) {
    if (!mode.embeddedRegions) {
        return {};
    }
    const std::vector<InjectionRegion> regions = mode.embeddedRegions(bufferText);
    if (regions.empty()) {
        return {};
    }

    const text::Rope              hostRope(bufferText);
    std::vector<EmbeddedDocument> documents;
    for (auto& [language, ranges] : GroupRangesByLanguage(regions)) {
        std::string padded;
        padded.reserve(bufferText.size());
        std::size_t offset = 0;
        while (offset < bufferText.size()) {
            const text::Rope::DecodedCodepoint decoded = hostRope.CodepointAt(offset);
            if (decoded.codepoint == U'\n' || ByteInRanges(offset, ranges)) {
                padded.append(bufferText.substr(offset, decoded.byteLength));
            }
            else {
                padded.append(FillerForByteLength(decoded.byteLength));
            }
            offset += decoded.byteLength;
        }
        documents.push_back(EmbeddedDocument{.language = language, .documentText = std::move(padded), .ownedRanges = ranges});
    }
    return documents;
}

std::optional<std::string> EmbeddedLanguageAtByteOffset(const std::vector<EmbeddedDocument>& documents,
                                                        std::size_t                          byteOffset) {
    for (const EmbeddedDocument& document : documents) {
        if (ByteInRanges(byteOffset, document.ownedRanges)) {
            return document.language;
        }
    }
    return std::nullopt;
}

std::string ResolveLspServerKey(const Mode& mode, std::string_view bufferText, std::size_t byteOffset) {
    const std::vector<EmbeddedDocument> documents = BuildEmbeddedDocuments(mode, bufferText);
    if (const std::optional<std::string> language = EmbeddedLanguageAtByteOffset(documents, byteOffset)) {
        return *language;
    }
    return {};
}

} // namespace ned::editor
