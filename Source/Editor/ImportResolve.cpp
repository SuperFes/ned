#include "ImportResolve.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Editor/BazelLabels.h"
#include "Editor/CueModules.h"
#include "Editor/FlatModules.h"
#include "Editor/GoModules.h"
#include "Editor/ImportResolutionConfig.h"
#include "Editor/Lsp/RootResolver.h"
#include "Editor/NodeModules.h"
#include "Editor/OdinCollections.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Settings.h"
#include "Editor/ToolchainIncludePaths.h"
#include "Text/FileUri.h"

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

    std::filesystem::path PackageRootFor(const std::filesystem::path& importingFile, const std::string& languageKey) {
        return importingFile.empty() ? ProjectRoot() : lsp::ResolveLspRoot(importingFile, languageKey);
    }

    // The library directory .dart_tool/package_config.json maps `name` to:
    // each package's rootUri (relative to the .dart_tool directory, or a
    // file: URI) joined with its packageUri. nullopt when the file has no
    // such package or can't be read.
    std::optional<std::filesystem::path> PackageConfigDirectory(const std::filesystem::path& configFile,
                                                                const std::string&           name) {
        std::ifstream in(configFile);
        if (!in) {
            return std::nullopt;
        }
        const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
        if (!config.is_object() || !config.contains("packages") || !config["packages"].is_array()) {
            return std::nullopt;
        }
        for (const nlohmann::json& package : config["packages"]) {
            if (!package.is_object() || package.value("name", "") != name) {
                continue;
            }
            const std::string     rootUri = package.value("rootUri", "");
            std::filesystem::path root    = text::FileUriToPath(rootUri).value_or(std::filesystem::path(rootUri));
            if (root.is_relative()) {
                root = configFile.parent_path() / root;
            }
            return (root / package.value("packageUri", "")).lexically_normal();
        }
        return std::nullopt;
    }

    // The `name:` a pubspec.yaml gives its own package.
    std::string PubspecName(const std::filesystem::path& pubspec) {
        std::ifstream in(pubspec);
        std::string   line;
        while (std::getline(in, line)) {
            if (line.rfind("name:", 0) != 0) {
                continue;
            }
            std::string name = line.substr(5);
            name             = name.substr(0, name.find('#'));
            std::erase_if(name, [](char c) { return c == ' ' || c == '\t' || c == '\'' || c == '"' || c == '\r'; });
            return name;
        }
        return {};
    }

    // Where "package:name/" points from `start`, walking upward: the first
    // package_config.json decides; below it, a pubspec.yaml naming `name`
    // is its own package's lib/.
    std::optional<std::filesystem::path> DartPackageDirectory(const std::filesystem::path& start,
                                                              const std::string&           name) {
        std::error_code ec;
        for (std::filesystem::path dir = start;; dir = dir.parent_path()) {
            if (const std::filesystem::path config = dir / ".dart_tool" / "package_config.json";
                std::filesystem::exists(config, ec)) {
                return PackageConfigDirectory(config, name);
            }
            if (const std::filesystem::path pubspec = dir / "pubspec.yaml";
                std::filesystem::exists(pubspec, ec) && PubspecName(pubspec) == name) {
                return dir / "lib";
            }
            if (dir == dir.parent_path() || dir.empty()) {
                return std::nullopt;
            }
        }
    }

    // The file a package directory opens as (ImportResolutionConfig's
    // packageDirectories); a path that isn't a directory is its own answer.
    std::optional<std::filesystem::path> PackageFileFor(const std::filesystem::path&  path,
                                                        const ImportResolutionConfig& config) {
        std::error_code ec;
        if (!config.packageDirectories || !std::filesystem::is_directory(path, ec)) {
            return path;
        }
        const std::string directoryName = path.filename().string();
        for (const std::string& extension : config.extensions) {
            for (const std::string& stem : {std::string("doc"), directoryName}) {
                if (const std::filesystem::path file = path / (stem + "." + extension);
                    std::filesystem::is_regular_file(file, ec)) {
                    return file;
                }
            }
        }
        std::optional<std::filesystem::path> first;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            const std::filesystem::path& file = entry.path();
            const std::string            stem = file.stem().string();
            if (!entry.is_regular_file(ec) || stem.ends_with("_test")) {
                continue;
            }
            const std::string extension = file.extension().string();
            const bool        ours      = std::ranges::any_of(config.extensions, [&](const std::string& candidate) {
                return extension == "." + candidate;
            });
            if (ours && (!first || file < *first)) {
                first = file;
            }
        }
        return first;
    }

    // Each source root under the importing file's package root, then under
    // the project root -- a monorepo's package keeps its own "src/".
    std::vector<std::filesystem::path> SourceRootDirectories(const ImportResolutionConfig& config,
                                                             const std::filesystem::path&  importingFile,
                                                             const std::string&            languageKey) {
        std::vector<std::filesystem::path> bases{PackageRootFor(importingFile, languageKey)};
        if (bases.front() != ProjectRoot()) {
            bases.push_back(ProjectRoot());
        }
        std::vector<std::filesystem::path> directories;
        for (const std::filesystem::path& base : bases) {
            for (const std::string& root : config.sourceRoots) {
                directories.push_back(base / root);
            }
        }
        return directories;
    }

    SearchParameters ParametersFor(const std::filesystem::path& baseDirectory,
                                   const std::filesystem::path& importingFile, const Mode& mode) {
        const ProjectSettings projectSettings = LoadProjectSettings(ProjectRoot());
        const std::string     languageKey     = LanguageKeyForMode(mode);
        SearchParameters      parameters;
        parameters.config = ResolveImportResolutionConfig(projectSettings, languageKey);

        // Source roots are the language's own layout, so they come before
        // anything the project or toolchain adds.
        parameters.includePaths = SourceRootDirectories(parameters.config, importingFile, languageKey);

        // toolchain-include-paths follow-up: project-configured includePaths
        // always come first (a user override outranks a guessed default),
        // with the real compiler's own system search paths appended as a
        // last-resort fallback for an angle-form/system include
        // ProjectSettings never mentioned at all.
        const std::vector<std::filesystem::path>& projectPaths = IncludePathsForMode(projectSettings, mode.name);
        parameters.includePaths.insert(parameters.includePaths.end(), projectPaths.begin(), projectPaths.end());
        const std::vector<std::filesystem::path> toolchainPaths = ToolchainIncludePathsForLanguage(languageKey);
        parameters.includePaths.insert(parameters.includePaths.end(), toolchainPaths.begin(), toolchainPaths.end());

        // import-target-tree-sitter follow-up: per-language extension/
        // index-file/package-dir parameters (Editor/ImportResolutionConfig.h)
        // widen what ResolveFileLink can find beyond an exact on-disk match
        // -- a relative JS/TS import written without its real extension, a
        // Python package's __init__.py, a bare "import x from 'lodash'".
        if (parameters.config.searchPackageDirs) {
            const std::vector<std::filesystem::path> packageDirs = NodeModulesSearchPaths(baseDirectory, ProjectRoot());
            parameters.includePaths.insert(parameters.includePaths.end(), packageDirs.begin(), packageDirs.end());
        }
        return parameters;
    }

    // Elixir's Macro.underscore: "HTTPClient" -> "http_client", "MyApp" ->
    // "my_app". An underscore goes before a capital that ends a lowercase or
    // digit run, or that starts a word after an acronym.
    std::string SnakeCase(std::string_view step) {
        const auto  upper = [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
        const auto  lower = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
        const auto  digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
        std::string snake;
        for (std::size_t i = 0; i < step.size(); ++i) {
            const char c = step[i];
            if (i > 0 && upper(c)) {
                const char previous = step[i - 1];
                const bool startsWord =
                    lower(previous) || digit(previous) || (upper(previous) && i + 1 < step.size() && lower(step[i + 1]));
                if (startsWord) {
                    snake += '_';
                }
            }
            snake += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return snake;
    }

} // namespace

ImportResolutionConfig ImportResolutionConfigFor(const Mode& mode) {
    return ResolveImportResolutionConfig(LoadProjectSettings(ProjectRoot()), LanguageKeyForMode(mode));
}

std::string ModulePathToFilePath(std::string_view module, const ImportResolutionConfig& config) {
    const std::string_view separator = config.moduleSeparator.empty() ? std::string_view(".") : config.moduleSeparator;
    std::string            path;
    std::size_t            start = 0;
    while (true) {
        const std::size_t end  = module.find(separator, start);
        std::string       step = std::string(module.substr(start, end == std::string_view::npos ? end : end - start));
        if (config.snakeCaseSteps) {
            step = SnakeCase(step);
        }
        for (const auto& [from, to] : config.moduleSubstitutions) {
            for (std::size_t at = step.find(from); at != std::string::npos; at = step.find(from, at + to.size())) {
                step.replace(at, from.size(), to);
            }
        }
        path += step;
        if (end == std::string_view::npos) {
            break;
        }
        path += config.moduleJoin.empty() ? std::string_view("/") : std::string_view(config.moduleJoin);
        start = end + separator.size();
    }
    return path;
}

link::DetectedLink ImportLinkFor(const ImportTarget& target, const ImportResolutionConfig& config) {
    return link::DetectedLink{
        .kind             = link::LinkKind::File,
        .target           = target.isModulePath ? ModulePathToFilePath(target.target, config) : target.target,
        .startByte        = target.startByte,
        .endByte          = target.endByte,
        .relativeLevel    = target.relativeLevel,
        .isModDeclaration = target.isModDeclaration,
    };
}

std::optional<PrefixedImport> MatchImportPrefix(const std::string& target, const std::filesystem::path& importingFile,
                                                const Mode& mode) {
    const ImportResolutionConfig config = ImportResolutionConfigFor(mode);
    if (config.homePrefix && target.starts_with("~/")) {
        const char* home = std::getenv("HOME");
        return PrefixedImport{.prefix    = "~/",
                              .remainder = target.substr(2),
                              .root      = home != nullptr ? std::filesystem::path(home) : std::filesystem::path{}};
    }
    if (config.odinCollections) {
        if (const std::optional<std::string> collection = OdinCollectionName(target)) {
            const std::filesystem::path start = importingFile.empty() ? ProjectRoot() : importingFile.parent_path();
            return PrefixedImport{.prefix    = *collection + ":",
                                  .remainder = target.substr(collection->size() + 1),
                                  .root      = OdinCollectionRoot(*collection, start, OdinRoot())};
        }
    }
    if (config.bazelLabels) {
        BazelImportRoot root = BazelImportRootFor(target, importingFile.empty() ? ProjectRoot() / "BUILD" : importingFile);
        return PrefixedImport{.prefix = std::move(root.prefix), .remainder = std::move(root.remainder), .root = std::move(root.root)};
    }
    if (config.cueModules) {
        CueImportRoot root = CueImportRootFor(target, importingFile.empty() ? ProjectRoot() : importingFile.parent_path());
        return PrefixedImport{.prefix = std::move(root.prefix), .remainder = std::move(root.remainder), .root = std::move(root.root)};
    }
    if (config.goModules) {
        GoImportRoot root = GoImportRootFor(target, importingFile.empty() ? ProjectRoot() : importingFile.parent_path());
        return PrefixedImport{.prefix = std::move(root.prefix), .remainder = std::move(root.remainder), .root = std::move(root.root)};
    }
    for (const auto& [prefix, directory] : config.rootPrefixes) {
        if (target.rfind(prefix, 0) == 0) {
            const std::filesystem::path packageRoot = PackageRootFor(importingFile, LanguageKeyForMode(mode));
            return PrefixedImport{.prefix    = prefix,
                                  .remainder = target.substr(prefix.size()),
                                  .root      = directory.empty() ? packageRoot : packageRoot / directory};
        }
    }
    const std::string& scheme = config.packageScheme;
    if (!scheme.empty() && target.rfind(scheme, 0) == 0) {
        const std::size_t slash = target.find('/', scheme.size());
        if (slash == std::string::npos || slash == scheme.size()) {
            return PrefixedImport{.prefix = target, .remainder = {}, .root = {}};
        }
        const std::string           name  = target.substr(scheme.size(), slash - scheme.size());
        const std::filesystem::path start = importingFile.empty() ? ProjectRoot() : importingFile.parent_path();
        return PrefixedImport{.prefix    = target.substr(0, slash + 1),
                              .remainder = target.substr(slash + 1),
                              .root      = DartPackageDirectory(start, name).value_or(std::filesystem::path{})};
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> ImportSearchRoots(const link::DetectedLink&    detected,
                                                     const std::filesystem::path& importingFile, const Mode& mode) {
    if (const auto prefixed = MatchImportPrefix(detected.target, importingFile, mode)) {
        return prefixed->root.empty() ? std::vector<std::filesystem::path>{}
                                      : std::vector<std::filesystem::path>{prefixed->root};
    }
    const std::filesystem::path baseDirectory = BaseDirectoryFor(detected, importingFile);

    std::vector<std::filesystem::path> roots{baseDirectory, ProjectRoot()};
    const SearchParameters             parameters = ParametersFor(baseDirectory, importingFile, mode);
    roots.insert(roots.end(), parameters.includePaths.begin(), parameters.includePaths.end());
    return roots;
}

std::optional<ResolvedImport> ResolveImportLink(const link::DetectedLink&    detected,
                                                const std::filesystem::path& importingFile, const Mode& mode) {
    const std::filesystem::path baseDirectory = BaseDirectoryFor(detected, importingFile);
    const SearchParameters      parameters    = ParametersFor(baseDirectory, importingFile, mode);

    if (const auto prefixed = MatchImportPrefix(detected.target, importingFile, mode)) {
        if (prefixed->root.empty()) {
            return std::nullopt;
        }
        // A package directory's import may name its root outright (a Go
        // module's own top-level package).
        if (prefixed->remainder.empty()) {
            if (!parameters.config.packageDirectories) {
                return std::nullopt;
            }
            const auto file = PackageFileFor(prefixed->root, parameters.config);
            return file ? std::optional(ResolvedImport{.path = *file, .base = prefixed->root}) : std::nullopt;
        }
        // Absolute, so nothing but the prefix's own root is tried.
        const auto resolved =
            link::ResolveFileLink((prefixed->root / prefixed->remainder).string(), prefixed->root, {},
                                  parameters.config.extensions, parameters.config.indexBasenames, nullptr,
                                  parameters.config.partialPrefix);
        const auto file = resolved ? PackageFileFor(*resolved, parameters.config) : std::nullopt;
        if (!file) {
            return std::nullopt;
        }
        return ResolvedImport{.path = *file, .base = prefixed->root};
    }

    std::filesystem::path resolvedBase;
    const auto            resolved =
        link::ResolveFileLink(detected.target, baseDirectory, parameters.includePaths, parameters.config.extensions,
                              parameters.config.indexBasenames, &resolvedBase, parameters.config.partialPrefix);
    const auto file = resolved ? PackageFileFor(*resolved, parameters.config) : std::nullopt;
    if (file) {
        return ResolvedImport{.path = *file, .base = resolvedBase};
    }
    if (!parameters.config.flatModules) {
        return std::nullopt;
    }
    // Where each step is its own module ("Mylib.Bar"), the leftmost one a
    // file answers is the one the path goes through; a joined name (Ada's
    // "foo-bar") is one file or none, and a path with a directory in it is
    // no module name at all.
    std::vector<std::string> names;
    if (parameters.config.moduleJoin == "/") {
        for (std::size_t start = 0; start <= detected.target.size();) {
            const std::size_t end = std::min(detected.target.find('/', start), detected.target.size());
            names.push_back(detected.target.substr(start, end - start));
            start = end + 1;
        }
    }
    else if (detected.target.find('/') == std::string::npos) {
        names.push_back(detected.target);
    }
    std::vector<std::filesystem::path> roots{PackageRootFor(importingFile, LanguageKeyForMode(mode))};
    if (roots.front() != ProjectRoot()) {
        roots.push_back(ProjectRoot());
    }
    for (const std::string& name : names) {
        for (const std::filesystem::path& root : roots) {
            if (auto found = FindFlatModule(name, parameters.config.extensions, root)) {
                return ResolvedImport{.path = std::move(*found), .base = root};
            }
        }
    }
    return std::nullopt;
}

} // namespace ned::editor
