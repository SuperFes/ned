#include "PromptHistory.h"

#include <utility>

namespace ned::editor {

PromptHistory::PromptHistory(std::size_t capacityPerKind) : capacityPerKind_(capacityPerKind) {}

void PromptHistory::Record(std::string_view key, std::string entry) {
    if (entry.empty()) {
        return;
    }

    std::vector<std::string>& ring = rings_[std::string(key)];
    if (!ring.empty() && ring.front() == entry) {
        return;
    }

    ring.insert(ring.begin(), std::move(entry));
    if (ring.size() > capacityPerKind_) {
        ring.pop_back();
    }
}

const std::vector<std::string>& PromptHistory::Entries(std::string_view key) const {
    static const std::vector<std::string> kEmpty;
    const auto                            it = rings_.find(std::string(key));
    return it == rings_.end() ? kEmpty : it->second;
}

} // namespace ned::editor
