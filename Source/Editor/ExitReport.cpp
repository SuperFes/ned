#include "ExitReport.h"

#include <mutex>
#include <utility>

namespace ned::editor {

namespace {

    std::mutex reportMutex;

    std::vector<std::string>& Reports() {
        static std::vector<std::string> reports;
        return reports;
    }

} // namespace

void ReportOnExit(std::string message) {
    if (message.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(reportMutex);
    Reports().push_back(std::move(message));
}

std::vector<std::string> TakeExitReports() {
    const std::lock_guard<std::mutex> lock(reportMutex);
    return std::exchange(Reports(), {});
}

} // namespace ned::editor
