#include "VimGlobalMarks.h"

#include <array>
#include <mutex>

namespace ned::editor::vim {

namespace {

std::mutex& Mutex() {
    static std::mutex mutex;
    return mutex;
}

std::array<std::optional<GlobalMark>, 26>& Marks() {
    static std::array<std::optional<GlobalMark>, 26> marks;
    return marks;
}

} // namespace

void SetGlobalMark(char32_t name, GlobalMark mark) {
    const std::lock_guard<std::mutex> lock(Mutex());
    Marks()[static_cast<std::size_t>(name - U'A')] = std::move(mark);
}

std::optional<GlobalMark> GetGlobalMark(char32_t name) {
    const std::lock_guard<std::mutex> lock(Mutex());
    return Marks()[static_cast<std::size_t>(name - U'A')];
}

void ClearGlobalMarksForTesting() {
    const std::lock_guard<std::mutex> lock(Mutex());
    Marks().fill(std::nullopt);
}

} // namespace ned::editor::vim
