#include "BazelLabels.h"

#include <initializer_list>
#include <system_error>

namespace ned::editor {

namespace {

    bool HasAny(const std::filesystem::path& directory, std::initializer_list<std::string_view> names) {
        std::error_code ec;
        for (const std::string_view name : names) {
            if (std::filesystem::is_regular_file(directory / name, ec)) {
                return true;
            }
        }
        return false;
    }

    std::optional<std::filesystem::path> WorkspaceRoot(const std::filesystem::path& start) {
        for (std::filesystem::path dir = start.lexically_normal(); !dir.empty(); dir = dir.parent_path()) {
            if (HasAny(dir, {"MODULE.bazel", "REPO.bazel", "WORKSPACE", "WORKSPACE.bazel"})) {
                return dir;
            }
            if (dir == dir.parent_path()) {
                break;
            }
        }
        return std::nullopt;
    }

    // The nearest directory at or above `start` holding a BUILD file, no
    // higher than the workspace root.
    std::optional<std::filesystem::path> PackageDirectory(const std::filesystem::path& start) {
        const std::optional<std::filesystem::path> workspace = WorkspaceRoot(start);
        for (std::filesystem::path dir = start.lexically_normal(); !dir.empty(); dir = dir.parent_path()) {
            if (HasAny(dir, {"BUILD", "BUILD.bazel"})) {
                return dir;
            }
            if (dir == workspace || dir == dir.parent_path()) {
                break;
            }
        }
        return std::nullopt;
    }

    // "@//" and "@@//" name the main repository; any other "@name" is
    // another one, which only Bazel's own output base holds.
    std::optional<std::string_view> MainRepositoryPrefix(std::string_view label) {
        for (const std::string_view prefix : {"@@//", "@//", "//"}) {
            if (label.starts_with(prefix)) {
                return prefix;
            }
        }
        return std::nullopt;
    }

} // namespace

BazelImportRoot BazelImportRootFor(std::string_view label, const std::filesystem::path& importingFile) {
    const std::filesystem::path importingDirectory = importingFile.parent_path();
    const std::size_t           colon              = label.find(':');
    if (label.starts_with('@') || label.starts_with("//")) {
        const std::optional<std::string_view>      repository = MainRepositoryPrefix(label);
        const std::optional<std::filesystem::path> workspace  = WorkspaceRoot(importingDirectory);
        if (!repository || !workspace || colon == std::string_view::npos) {
            return {std::string(label), {}, {}};
        }
        const std::string_view package = label.substr(repository->size(), colon - repository->size());
        return {std::string(label.substr(0, colon + 1)), std::string(label.substr(colon + 1)), *workspace / package};
    }
    const std::size_t           prefixSize = colon == 0 ? 1 : 0;
    const std::filesystem::path package    = PackageDirectory(importingDirectory).value_or(importingDirectory);
    return {std::string(label.substr(0, prefixSize)), std::string(label.substr(prefixSize)), package};
}

std::optional<std::string> BazelLabelFor(const std::filesystem::path& file, const std::filesystem::path& importingFile,
                                         std::string_view original) {
    const std::optional<std::filesystem::path> package   = PackageDirectory(file.parent_path());
    const std::optional<std::filesystem::path> workspace = WorkspaceRoot(file.parent_path());
    if (!package || !workspace) {
        return std::nullopt;
    }
    const std::string                     name       = file.lexically_normal().lexically_relative(*package).generic_string();
    const std::optional<std::string_view> repository = MainRepositoryPrefix(original);
    if (!repository && PackageDirectory(importingFile.parent_path()) == package) {
        return (original.starts_with(':') ? ":" : "") + name;
    }
    std::string packagePath = package->lexically_relative(*workspace).generic_string();
    if (packagePath == ".") {
        packagePath.clear();
    }
    return std::string(repository.value_or("//")) + packagePath + ":" + name;
}

} // namespace ned::editor
