#include "RowStatus.h"

namespace ned::editor::vcs {

RowStatus ClassifyPorcelainStatus(const std::string& state) {
    if (state == "??") {
        return RowStatus::Untracked;
    }
    if (state.find('D') != std::string::npos) {
        return RowStatus::Deleted;
    }
    if (state.find('M') != std::string::npos) {
        return RowStatus::Modified;
    }
    if (state.find('A') != std::string::npos) {
        return RowStatus::Added;
    }
    return RowStatus::Modified;
}

bool IsUnmergedStatus(const std::string& state) {
    if (state == "??") {
        return false; // untracked: '?' in both columns, never an unmerged path
    }
    return state.find('U') != std::string::npos || state == "AA" || state == "DD";
}

StatusSections PartitionVcsStatus(const std::vector<StatusEntry>& entries) {
    StatusSections sections;
    for (const StatusEntry& entry : entries) {
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
