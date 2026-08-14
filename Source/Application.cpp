//
// Created by Fester on 5/29/25.
//

#include "Application.h"

#include <bits/ostream.tcc>
#include <iostream>

namespace Ned {
std::string Application::GetTitle() {
    return Title();
}

void Application::SetTitle(const std::string& title) {
    Title(title);
}

void Application::SetTitle(const char* title) {
    Title(title);
}

std::mutex& Application::Mutex() {
    static std::mutex mutex;

    return mutex;
}

std::string Application::Title(const std::string& title) {
    static std::string Title;

    if (title.empty()) {
        return Title;
    }

    std::lock_guard lock(Mutex());

    Title = title;

    return Title;
}


} // namespace Ned
