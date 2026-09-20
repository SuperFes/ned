//
// Created by Fester on 5/29/25.
//

#include "Application.h"

#include <set>
#include <string_view>
#include <utility>

namespace Ned {

namespace {

    // Guards everything in the exit-lifecycle block below. Separate from
    // Application::Mutex(), which the title owns: a background save thread
    // latching a failure has no business contending with a title update.
    std::mutex& ExitMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::vector<std::string>& Reports() {
        static std::vector<std::string> reports;
        return reports;
    }

    constexpr std::string_view kMessagePrefix = "ned: ";

    std::set<std::filesystem::path>& SaveFailures() {
        static std::set<std::filesystem::path> failures;
        return failures;
    }

} // namespace

auto Application::GetTitle() -> std::string {
    return Title();
}

void Application::SetTitle(const std::string& title) {
    Title(title);
}

void Application::SetTitle(const char* title) {
    Title(title);
}

auto Application::Mutex() -> std::mutex& {
    static std::mutex mutex;

    return mutex;
}

auto Application::Title(const std::string& title) -> std::string {
    static std::string Title;

    if (title.empty()) {
        return Title;
    }

    std::lock_guard lock(Mutex());

    Title = title;

    return Title;
}

void Application::ReportOnExit(std::string message) {
    if (message.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(ExitMutex());
    Reports().push_back(std::move(message));
}

auto Application::TakeExitReports() -> std::vector<std::string> {
    const std::lock_guard<std::mutex> lock(ExitMutex());
    return std::exchange(Reports(), {});
}

void Application::NoteSaveFailed(const std::filesystem::path& path, const std::string& bufferName, const std::string& reason) {
    std::string detail = reason;
    if (detail.starts_with(kMessagePrefix)) {
        detail.erase(0, kMessagePrefix.size());
    }

    {
        const std::lock_guard<std::mutex> lock(ExitMutex());
        SaveFailures().insert(path);
    }
    ReportOnExit(std::string(kMessagePrefix) + "failed to save \"" + bufferName + "\"" +
                 (detail.empty() ? std::string() : ": " + detail));
}

void Application::NoteSaveSucceeded(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(ExitMutex());
    SaveFailures().erase(path);
}

auto Application::CurrentExitStatus() -> ExitStatus {
    const std::lock_guard<std::mutex> lock(ExitMutex());
    return SaveFailures().empty() ? ExitStatus::Success : ExitStatus::SaveFailed;
}

void Application::ResetExitStateForTesting() {
    const std::lock_guard<std::mutex> lock(ExitMutex());
    Reports().clear();
    SaveFailures().clear();
}

} // namespace Ned
