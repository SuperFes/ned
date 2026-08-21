#include "BackgroundActivity.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace ned::editor {

namespace {

    struct ActivityState {
        int         count = 0;
        std::string detail;
    };

    std::mutex activityMutex;

    // std::map, not unordered_map: ActiveBackgroundActivities' sorted-by-name
    // contract falls out of iteration order for free.
    std::map<std::string, ActivityState>& Activities() {
        static std::map<std::string, ActivityState> activities;
        return activities;
    }

} // namespace

void BeginBackgroundActivity(const std::string& name) {
    const std::lock_guard<std::mutex> lock(activityMutex);
    ++Activities()[name].count;
}

void EndBackgroundActivity(const std::string& name) {
    const std::lock_guard<std::mutex> lock(activityMutex);
    auto&                             activities = Activities();
    const auto                        it         = activities.find(name);
    if (it == activities.end()) {
        return; // end without a begin -- clamp, see header comment
    }
    if (--it->second.count <= 0) {
        activities.erase(it); // detail dies with the entry -- see SetBackgroundActivityDetail's doc comment
    }
}

void SetBackgroundActivityDetail(const std::string& name, std::string detail) {
    const std::lock_guard<std::mutex> lock(activityMutex);
    auto&                             activities = Activities();
    const auto                        it         = activities.find(name);
    if (it == activities.end()) {
        return; // not active -- no entry to attach detail to
    }
    it->second.detail = std::move(detail);
}

std::vector<BackgroundActivity> ActiveBackgroundActivities() {
    const std::lock_guard<std::mutex> lock(activityMutex);
    std::vector<BackgroundActivity>   result;
    result.reserve(Activities().size());
    for (const auto& [name, state] : Activities()) {
        result.push_back(BackgroundActivity{.name = name, .detail = state.detail});
    }
    return result;
}

} // namespace ned::editor
