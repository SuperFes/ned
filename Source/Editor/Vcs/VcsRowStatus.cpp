#include "VcsRowStatus.h"

namespace ned::editor::vcs {

VcsRowStatus ClassifyPorcelainStatus(const std::string& state) {
    if (state == "??") {
        return VcsRowStatus::Untracked;
    }
    if (state.find('D') != std::string::npos) {
        return VcsRowStatus::Deleted;
    }
    if (state.find('M') != std::string::npos) {
        return VcsRowStatus::Modified;
    }
    if (state.find('A') != std::string::npos) {
        return VcsRowStatus::Added;
    }
    return VcsRowStatus::Modified;
}

VcsStatusSections PartitionVcsStatus(const std::vector<VcsStatusEntry>& entries) {
    VcsStatusSections sections;
    for (const VcsStatusEntry& entry : entries) {
        if (entry.state == "??") {
            sections.untracked.push_back(entry);
            continue;
        }
        const char index    = !entry.state.empty() ? entry.state[0] : ' ';
        const char worktree = entry.state.size() > 1 ? entry.state[1] : ' ';
        if (index != ' ') {
            sections.staged.push_back(entry);
        }
        if (worktree != ' ') {
            sections.unstaged.push_back(entry);
        }
    }
    return sections;
}

} // namespace ned::editor::vcs
