#include "DataDir.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#ifndef NED_INSTALL_DATADIR
#error "NED_INSTALL_DATADIR must be defined by the build (CMake/DataTree.cmake)"
#endif

namespace ned::editor {

namespace {

    bool HoldsDataTree(const std::filesystem::path& candidate) {
        std::error_code ec;
        return std::filesystem::is_directory(candidate / "languages", ec);
    }

} // namespace

DataDirCandidates DefaultDataDirCandidates() {
    DataDirCandidates candidates;
    if (const char* env = std::getenv("NED_DATA_DIR"); env && *env) {
        candidates.environment = std::filesystem::path(env);
    }
    std::error_code             ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && exe.has_parent_path()) {
        candidates.executableRelative = exe.parent_path().parent_path() / "share" / "ned";
    }
    candidates.installed = std::filesystem::path(NED_INSTALL_DATADIR);
    return candidates;
}

std::filesystem::path ResolveDataDir(const DataDirCandidates& candidates) {
    if (candidates.environment) {
        if (HoldsDataTree(*candidates.environment)) {
            return *candidates.environment;
        }
        throw std::runtime_error("NED_DATA_DIR=" + candidates.environment->string() +
                                 " does not contain a languages/ directory");
    }
    std::string looked;
    if (candidates.executableRelative) {
        if (HoldsDataTree(*candidates.executableRelative)) {
            return *candidates.executableRelative;
        }
        looked += candidates.executableRelative->string() + ", ";
    }
    if (HoldsDataTree(candidates.installed)) {
        return candidates.installed;
    }
    looked += candidates.installed.string();
    throw std::runtime_error("ned data directory not found (looked in " + looked +
                             "; set NED_DATA_DIR to the directory holding languages/ and plugins/)");
}

const std::filesystem::path& DataDir() {
    static const std::filesystem::path kDir = ResolveDataDir(DefaultDataDirCandidates());
    return kDir;
}

} // namespace ned::editor
