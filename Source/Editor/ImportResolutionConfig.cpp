#include "ImportResolutionConfig.h"

#include <unordered_map>

#include "ProjectSettings.h"

namespace ned::editor {

namespace {

    const std::unordered_map<std::string, ImportResolutionConfig>& BundledDefaults() {
        static const std::unordered_map<std::string, ImportResolutionConfig> defaults = {
            {"php", {.extensions = {"php"}}},
            {"javascript", {.extensions = {"js", "jsx", "mjs", "cjs"}, .indexBasenames = {"index"}, .searchPackageDirs = true}},
            {"typescript",
             {.extensions = {"ts", "tsx", "js", "jsx", "mjs", "cjs"}, .indexBasenames = {"index"}, .searchPackageDirs = true}},
            {"tsx", {.extensions = {"ts", "tsx", "js", "jsx", "mjs", "cjs"}, .indexBasenames = {"index"}, .searchPackageDirs = true}},
            {"python", {.extensions = {"py"}, .indexBasenames = {"__init__"}}},
            {"bash", {.extensions = {"sh"}}},
            {"clojure", {.extensions = {"clj", "cljc", "cljs"}}},
            {"jank", {.extensions = {"clj", "cljc", "cljs"}}},
            {"css", {.extensions = {"css"}}},
            {"janet", {.extensions = {"janet"}}},
            // resolver-gaps follow-up: only ever consulted for Rust's own
            // "mod foo;" file-per-module declaration (rust-imports.scm's
            // @import.moddecl) -- "foo" tried as "foo.rs" (extension) or
            // "foo/mod.rs" (indexBasename), against a baseDirectory
            // BufferView has already adjusted for the importing file's own
            // stem (Mode.h's ImportTarget::isModDeclaration doc comment).
            {"rust", {.extensions = {"rs"}, .indexBasenames = {"mod"}}},
        };
        return defaults;
    }

} // namespace

ImportResolutionConfig DefaultImportResolutionConfig(const std::string& languageKey) {
    const auto& defaults = BundledDefaults();
    if (const auto it = defaults.find(languageKey); it != defaults.end()) {
        return it->second;
    }
    return {};
}

ImportResolutionConfig ResolveImportResolutionConfig(const ProjectSettings& settings, const std::string& languageKey) {
    ImportResolutionConfig            config   = DefaultImportResolutionConfig(languageKey);
    const ImportResolutionOverride& override_ = ImportResolutionOverrideForLanguage(settings, languageKey);
    if (!override_.extensions.empty()) {
        config.extensions = override_.extensions;
    }
    if (!override_.indexBasenames.empty()) {
        config.indexBasenames = override_.indexBasenames;
    }
    if (override_.searchPackageDirs.has_value()) {
        config.searchPackageDirs = *override_.searchPackageDirs;
    }
    return config;
}

} // namespace ned::editor
