#include "ScratchPad.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace ned::editor {

namespace {

    constexpr std::string_view kExtension = ".txt";

    std::mutex& AutoSaveMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoSaveStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

std::filesystem::path ScratchDirectory() {
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
        return std::filesystem::path(xdgDataHome) / "ned" / "scratches";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "ned" / "scratches";
    }

    throw std::runtime_error("ned: cannot determine data directory (neither XDG_DATA_HOME nor HOME is set)");
}

bool IsValidScratchName(std::string_view name) {
    return !name.empty() && name.find('/') == std::string_view::npos;
}

std::filesystem::path ScratchPathForName(std::string_view name) {
    if (!IsValidScratchName(name)) {
        throw std::invalid_argument("ned: invalid scratch name: \"" + std::string(name) + "\"");
    }
    return ScratchDirectory() / (std::string(name) + std::string(kExtension));
}

std::vector<std::string> ListScratchNames() {
    std::vector<std::string> names;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(ScratchDirectory())) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path path = entry.path();
            if (path.extension() == kExtension) {
                names.push_back(path.stem().string());
            }
        }
    }
    catch (const std::filesystem::filesystem_error&) {
        return {}; // directory doesn't exist, no permission, etc. -- no candidates
    }

    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> CompleteScratchNames(std::string_view prefix) {
    std::vector<std::string> matches;
    for (std::string& name : ListScratchNames()) {
        if (name.size() >= prefix.size() && std::string_view(name).substr(0, prefix.size()) == prefix) {
            matches.push_back(std::move(name));
        }
    }
    return matches; // ListScratchNames() is already sorted
}

void SetScratchAutoSaveEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(AutoSaveMutex());
    AutoSaveStorage() = enabled;
}

bool ScratchAutoSaveEnabled() {
    const std::lock_guard<std::mutex> lock(AutoSaveMutex());
    return AutoSaveStorage();
}

void AutoSaveScratchBuffers(text::BufferList& bufferList) {
    if (!ScratchAutoSaveEnabled()) {
        return;
    }

    const std::filesystem::path scratchDirectory = ScratchDirectory();
    std::filesystem::create_directories(scratchDirectory);
    const std::filesystem::path canonicalScratchDirectory = std::filesystem::weakly_canonical(scratchDirectory);

    for (const auto& buffer : bufferList.Buffers()) {
        if (!buffer->Modified() || !buffer->Path().has_value()) {
            continue;
        }
        if (std::filesystem::weakly_canonical(buffer->Path()->parent_path()) != canonicalScratchDirectory) {
            continue;
        }
        try {
            buffer->Save();
        }
        catch (const std::exception&) {
            // Swallowed -- see this function's own header comment for why.
        }
    }
}

} // namespace ned::editor
