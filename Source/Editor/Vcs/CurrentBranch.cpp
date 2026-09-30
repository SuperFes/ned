#include "CurrentBranch.h"

#include <map>
#include <mutex>

namespace ned::editor::vcs {

namespace {

    struct State {
        std::mutex                                   mutex;
        std::map<std::filesystem::path, std::string> branches;
    };

    State& Branches() {
        static State state;
        return state;
    }

} // namespace

void SetCurrentBranch(const std::filesystem::path& root, std::optional<std::string> branch) {
    State&                state = Branches();
    const std::lock_guard lock(state.mutex);
    if (branch) {
        state.branches.insert_or_assign(root, std::move(*branch));
    }
    else {
        state.branches.erase(root);
    }
}

std::optional<std::string> CurrentBranch(const std::filesystem::path& root) {
    State&                state = Branches();
    const std::lock_guard lock(state.mutex);
    const auto            found = state.branches.find(root);
    if (found == state.branches.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ClearCurrentBranches() {
    State&                state = Branches();
    const std::lock_guard lock(state.mutex);
    state.branches.clear();
}

} // namespace ned::editor::vcs
