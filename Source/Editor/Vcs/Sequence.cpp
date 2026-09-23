#include "Sequence.h"

#include <fstream>
#include <iterator>
#include <mutex>

#include "RowStatus.h"
#include "Text/ThreeWayMerge.h"

namespace ned::editor::vcs {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoStageFlag() {
        static bool enabled = false;
        return enabled;
    }

    std::string JoinRelative(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& root) {
        std::string joined;
        for (const std::filesystem::path& path : paths) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += path.lexically_relative(root).string();
        }
        return joined;
    }

} // namespace

void SetSequenceAutoStage(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    AutoStageFlag() = enabled;
}

bool SequenceAutoStageEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return AutoStageFlag();
}

std::string SequenceLabel(const SequenceState& state) {
    std::string label;
    if (state.kind == "rebase") {
        label = "Rebasing";
    }
    else if (state.kind == "merge") {
        label = "Merging";
    }
    else if (state.kind == "cherry-pick") {
        label = "Cherry-picking";
    }
    else if (state.kind == "revert") {
        label = "Reverting";
    }
    else if (state.kind == "am") {
        label = "Applying patches";
    }
    else {
        label = state.kind;
    }
    if (!label.empty() && state.total > 0) {
        label += " " + std::to_string(state.step) + "/" + std::to_string(state.total);
    }
    return label;
}

SequenceContinuePlan PlanSequenceContinue(const std::vector<StatusEntry>& status, const std::filesystem::path& root,
                                          const SequenceFileProbe& probe, bool autoStage) {
    SequenceContinuePlan plan;
    plan.autoStage = autoStage;
    for (const StatusEntry& entry : status) {
        if (!IsUnmergedStatus(entry.state)) {
            continue;
        }
        const std::filesystem::path path = (root / entry.path).lexically_normal();
        if (probe.hasUnsavedBuffer(path)) {
            plan.unsaved.push_back(path);
        }
        else if (probe.hasConflictMarkers(path)) {
            plan.conflicted.push_back(path);
        }
        else {
            plan.toStage.push_back(path);
        }
    }
    return plan;
}

std::string SequenceContinueBlockedMessage(const SequenceContinuePlan& plan, const std::filesystem::path& root) {
    if (!plan.unsaved.empty()) {
        return "Save first: " + JoinRelative(plan.unsaved, root);
    }
    if (!plan.conflicted.empty()) {
        return "Still conflicted: " + JoinRelative(plan.conflicted, root);
    }
    if (!plan.toStage.empty() && !plan.autoStage) {
        return "Resolved but unstaged: " + JoinRelative(plan.toStage, root) +
               " -- stage them, or (ned/set-vcs-sequence-auto-stage true)";
    }
    return {};
}

bool FileHasConflictMarkers(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return text::HasConflictMarkers(content);
}

} // namespace ned::editor::vcs
