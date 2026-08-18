#include "Languages.h"

// Every bundled grammar's C entry point, per tree-sitter's own
// `tree_sitter_<name>()` convention -- forward-declared here rather than via
// a grammar-provided header, the same way every tree-sitter consumer (Neovim,
// Emacs, ...) does it; grammar repos don't ship a public C++-facing header
// for this, just the compiled symbol. Names verified against each grammar's
// own generated src/parser.c rather than assumed -- tree-sitter-typescript's
// repo produces two symbols (tree_sitter_typescript/tree_sitter_tsx) from one
// clone, and tree-sitter-janet-simple's is tree_sitter_janet_simple, not
// tree_sitter_janet.
extern "C" {
const TSLanguage* tree_sitter_json(void);
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
const TSLanguage* tree_sitter_php(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_typescript(void);
const TSLanguage* tree_sitter_tsx(void);
const TSLanguage* tree_sitter_html(void);
const TSLanguage* tree_sitter_css(void);
const TSLanguage* tree_sitter_python(void);
const TSLanguage* tree_sitter_bash(void);
const TSLanguage* tree_sitter_janet_simple(void);
const TSLanguage* tree_sitter_markdown(void);
const TSLanguage* tree_sitter_org(void);
}

namespace ned::editor::treesitter {

std::optional<Language> LanguageByName(std::string_view name) {
    if (name == "json") {
        return Language(tree_sitter_json());
    }
    if (name == "c") {
        return Language(tree_sitter_c());
    }
    if (name == "cpp") {
        return Language(tree_sitter_cpp());
    }
    if (name == "php") {
        return Language(tree_sitter_php());
    }
    if (name == "javascript") {
        return Language(tree_sitter_javascript());
    }
    if (name == "typescript") {
        return Language(tree_sitter_typescript());
    }
    if (name == "tsx") {
        return Language(tree_sitter_tsx());
    }
    if (name == "html") {
        return Language(tree_sitter_html());
    }
    if (name == "css") {
        return Language(tree_sitter_css());
    }
    if (name == "python") {
        return Language(tree_sitter_python());
    }
    if (name == "bash") {
        return Language(tree_sitter_bash());
    }
    // sogaiu/tree-sitter-janet-simple, not GrayJack/tree-sitter-janet --
    // both exist; janet-simple was chosen for being the one already reused
    // by a real Emacs tree-sitter major mode (janet-ts-mode), the closest
    // available signal of real-world use over GrayJack's "supports higher
    // level constructs" but otherwise less externally validated grammar.
    if (name == "janet") {
        return Language(tree_sitter_janet_simple());
    }
    if (name == "markdown") {
        return Language(tree_sitter_markdown());
    }
    // Org-mode syntax-highlighting follow-up: nvim-orgmode/tree-sitter-org,
    // forked (not the real upstream repository) -- see CMakeLists.txt's own
    // ned_add_treesitter_grammar(tree-sitter-org ...) call for why.
    if (name == "org") {
        return Language(tree_sitter_org());
    }
    return std::nullopt;
}

} // namespace ned::editor::treesitter
