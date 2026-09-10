#include "UI/BufferView/CandidateList.h"

#include <algorithm>
#include <utility>

#include "Editor/FuzzyMatch.h"

namespace ned::ui::bufferview {

namespace {

    const std::string& EmptyString() {
        static const std::string empty;
        return empty;
    }

} // namespace

void CandidateList::Reset(std::vector<std::string> candidates, std::string_view query, std::size_t pinnedCount) {
    source_      = std::move(candidates);
    pinnedCount_ = std::min(pinnedCount, source_.size());
    RankAgainst(query, /*keepSelection=*/false);
}

void CandidateList::Refilter(std::string_view query) {
    RankAgainst(query, /*keepSelection=*/true);
}

void CandidateList::Refilter(std::vector<std::string> candidates, std::string_view query) {
    source_ = std::move(candidates);
    // Nominally the same pool (see the header), so the pin count carries over
    // -- but it can no longer point past the end.
    pinnedCount_ = std::min(pinnedCount_, source_.size());
    RankAgainst(query, /*keepSelection=*/true);
}

const std::vector<std::string>& CandidateList::Refiltered(std::string_view query) {
    Refilter(query);
    return ranked_;
}

const std::vector<std::string>& CandidateList::Refiltered(std::vector<std::string> candidates, std::string_view query) {
    Refilter(std::move(candidates), query);
    return ranked_;
}

void CandidateList::RankAgainst(std::string_view query, bool keepSelection) {
    if (pinnedCount_ == 0) {
        ranked_ = editor::FuzzyFilterAndRank(source_, std::string(query));
    }
    else {
        // Pinned entries keep the caller's own order rather than being ranked
        // among themselves -- they are a fixed little menu, not results.
        ranked_.clear();
        for (std::size_t i = 0; i < pinnedCount_; ++i) {
            if (editor::FuzzyScore(source_[i], query)) {
                ranked_.push_back(source_[i]);
            }
        }
        const std::vector<std::string> rest(source_.begin() + static_cast<std::ptrdiff_t>(pinnedCount_), source_.end());
        for (std::string& name : editor::FuzzyFilterAndRank(rest, std::string(query))) {
            ranked_.push_back(std::move(name));
        }
    }
    if (!keepSelection || ranked_.empty()) {
        selection_ = 0;
        return;
    }
    // Clamp rather than reset: narrowing usually keeps the entry the user was
    // heading for in the list, just at a different index.
    selection_ = std::min(selection_, ranked_.size() - 1);
}

const std::string& CandidateList::Selected() const {
    if (ranked_.empty()) {
        return EmptyString();
    }
    return ranked_[std::min(selection_, ranked_.size() - 1)];
}

void CandidateList::SelectNext() {
    if (ranked_.empty()) {
        return;
    }
    selection_ = (selection_ + 1) % ranked_.size();
}

void CandidateList::SelectPrevious() {
    if (ranked_.empty()) {
        return;
    }
    selection_ = (selection_ + ranked_.size() - 1) % ranked_.size();
}

void CandidateList::SelectIndex(std::size_t index) {
    if (index < ranked_.size()) {
        selection_ = index;
    }
}

void CandidateList::Clear() {
    source_.clear();
    ranked_.clear();
    selection_ = 0;
}

} // namespace ned::ui::bufferview
