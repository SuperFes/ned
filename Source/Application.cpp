//
// Created by Fester on 5/29/25.
//

#include "Application.h"

#include <bits/ostream.tcc>

namespace Ned {
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


} // namespace Ned
