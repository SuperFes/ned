#include "IncrementalParse.h"

#include <algorithm>
#include <utility>

namespace ned::editor::grammar {

const Tree& IncrementalParseCache::Update(const Parser& parser, std::string_view bufferText) {
    if (lastTree_.has_value() && lastText_ == bufferText) {
        return *lastTree_;
    }

    ++generation_;
    if (!lastTree_.has_value()) {
        lastTree_ = parser.Parse(bufferText);
        lastText_.assign(bufferText);
        generationEdit_ = std::nullopt;
        return *lastTree_;
    }

    const std::string_view oldText = lastText_;
    const std::string_view newText = bufferText;

    const std::size_t maxCommon = std::min(oldText.size(), newText.size());
    std::size_t       prefix    = 0;
    while (prefix < maxCommon && oldText[prefix] == newText[prefix]) {
        ++prefix;
    }
    const std::size_t maxSuffix = maxCommon - prefix; // caps prefix+suffix at maxCommon, so they can't overlap
    std::size_t       suffix    = 0;
    while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
        ++suffix;
    }

    const text::ChangedSpan span{
        .oldStart = prefix,
        .oldEnd   = oldText.size() - suffix,
        .newStart = prefix,
        .newEnd   = newText.size() - suffix,
    };

    lastTree_->Edit(InputEditFor(oldText, newText, span));
    Tree reparsed   = parser.Parse(newText, *lastTree_);
    generationEdit_ = DescribeTreeEdit(*lastTree_, reparsed, span);
    lastTree_       = std::move(reparsed);
    lastText_.assign(newText);
    return *lastTree_;
}

} // namespace ned::editor::grammar
