#include "ImportResolve.h"

#include "Editor/ImportResolutionConfig.h"
#include "Editor/NodeModules.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Settings.h"
#include "Editor/ToolchainIncludePaths.h"

namespace ned::editor {

namespace {

    std::filesystem::path BaseDirectoryFor(const link::DetectedLink& detected, const std::filesystem::path& importingFile) {
        std::filesystem::path baseDirectory = importingFile.empty() ? ProjectRoot() : importingFile.parent_path();
        // resolver-gaps follow-up: Python's own leading-dot relative-import
        // level -- 1 dot means "this file's own directory" (baseDirectory
        // already computed above, no ascension), each additional dot ascends
        // one more parent directory (Mode.h's ImportTarget::relativeLevel doc
        // comment has the full semantics). Stops early if parent_path() stops
        // making progress (the filesystem root), the same guard
        // NodeModules.cpp's own upward walk uses.
        for (int level = 1; level < detected.relativeLevel; ++level) {
            const std::filesystem::path parent = baseDirectory.parent_path();
            if (parent == baseDirectory) {
                break;
            }
            baseDirectory = parent;
        }
        // resolver-gaps follow-up: Rust's own bodyless "mod foo;" declaration
        // (Mode.h's ImportTarget::isModDeclaration doc comment) -- a submodule
        // of any file other than a crate root/mod.rs lives one directory level
        // below the importing file, under a subdirectory named after that
        // file's own stem (e.g. "src/foo.rs"'s own "mod bar;" resolves against
        // "src/foo/bar.rs", not "src/bar.rs"). "main"/"lib"/"mod" are Rust's own
        // three file-name conventions where the importing file already sits at
        // the level its submodules resolve from, so no adjustment applies.
        if (detected.isModDeclaration && !importingFile.empty()) {
            const std::string stem = importingFile.stem().string();
            if (stem != "main" && stem != "lib" && stem != "mod") {
                baseDirectory /= stem;
            }
        }
        return baseDirectory;
    }

    // The rest of the search: every root tried after baseDirectory, plus the
    // per-language widening that decides what counts as a hit.
    struct SearchParameters {
        std::vector<std::filesystem::path> includePaths;
        ImportResolutionConfig             config;
    };

    SearchParameters ParametersFor(const std::filesystem::path& baseDirectory, const Mode& mode) {
        const ProjectSettings projectSettings = LoadProjectSettings(ProjectRoot());

        // toolchain-include-paths follow-up: project-configured includePaths
        // always come first (a user override outranks a guessed default),
        // with the real compiler's own system search paths appended as a
        // last-resort fallback for an angle-form/system include
        // ProjectSettings never mentioned at all.
        const std::string languageKey = LanguageKeyForMode(mode);
        SearchParameters  parameters;
        parameters.includePaths                                 = IncludePathsForMode(projectSettings, mode.name);
        const std::vector<std::filesystem::path> toolchainPaths = ToolchainIncludePathsForLanguage(languageKey);
        parameters.includePaths.insert(parameters.includePaths.end(), toolchainPaths.begin(), toolchainPaths.end());

        // import-target-tree-sitter follow-up: per-language extension/
        // index-file/package-dir parameters (Editor/ImportResolutionConfig.h)
        // widen what ResolveFileLink can find beyond an exact on-disk match
        // -- a relative JS/TS import written without its real extension, a
        // Python package's __init__.py, a bare "import x from 'lodash'".
        parameters.config = ResolveImportResolutionConfig(projectSettings, languageKey);
        if (parameters.config.searchPackageDirs) {
            const std::vector<std::filesystem::path> packageDirs = NodeModulesSearchPaths(baseDirectory, ProjectRoot());
            parameters.includePaths.insert(parameters.includePaths.end(), packageDirs.begin(), packageDirs.end());
        }
        return parameters;
    }

} // namespace

std::vector<std::filesystem::path> ImportSearchRoots(const link::DetectedLink&    detected,
                                                     const std::filesystem::path& importingFile, const Mode& mode) {
    const std::filesystem::path baseDirectory = BaseDirectoryFor(detected, importingFile);

    std::vector<std::filesystem::path> roots{baseDirectory, ProjectRoot()};
    const SearchParameters             parameters = ParametersFor(baseDirectory, mode);
    roots.insert(roots.end(), parameters.includePaths.begin(), parameters.includePaths.end());
    return roots;
}

std::optional<ResolvedImport> ResolveImportLink(const link::DetectedLink&    detected,
                                                const std::filesystem::path& importingFile, const Mode& mode) {
    const std::filesystem::path baseDirectory = BaseDirectoryFor(detected, importingFile);
    const SearchParameters      parameters    = ParametersFor(baseDirectory, mode);

    std::filesystem::path resolvedBase;
    const auto            resolved =
        link::ResolveFileLink(detected.target, baseDirectory, parameters.includePaths, parameters.config.extensions,
                              parameters.config.indexBasenames, &resolvedBase);
    if (!resolved) {
        return std::nullopt;
    }
    return ResolvedImport{.path = *resolved, .base = resolvedBase};
}

} // namespace ned::editor
