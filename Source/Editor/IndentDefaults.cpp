#include "IndentDefaults.h"

#include <unordered_map>

namespace ned::editor {

namespace {

    const std::unordered_map<std::string_view, IndentStyle>& BuiltinTable() {
        // clang-format off
        static const std::unordered_map<std::string_view, IndentStyle> table = {
            // -- a single, authoritative canonical formatter/spec exists --
            {"python",     {.useTabs = false, .width = 4}}, // PEP 8
            {"rust",       {.useTabs = false, .width = 4}}, // rustfmt default (non-configurable in stable rustfmt)
            {"go",         {.useTabs = true,  .width = 4}}, // gofmt mandates literal tabs; it has no width knob at all, so the width here is only an editor display convention, not part of the standard
            {"php",        {.useTabs = false, .width = 4}}, // PSR-12 SS2.2
            {"javascript", {.useTabs = false, .width = 2}}, // Prettier default, de facto ecosystem standard
            {"typescript", {.useTabs = false, .width = 2}}, // Prettier default
            {"tsx",        {.useTabs = false, .width = 2}}, // Prettier default (same as typescript)
            {"json",       {.useTabs = false, .width = 2}}, // matches the JS/npm ecosystem convention
            {"yaml",       {.useTabs = false, .width = 2}}, // the YAML spec forbids literal tab characters for indentation entirely -- not a style preference; overriding useTabs here produces invalid YAML, see Docs/FormattingRules.md
            {"kotlin",     {.useTabs = false, .width = 4}}, // official Kotlin coding conventions (kotlinlang.org)
            {"csharp",     {.useTabs = false, .width = 4}}, // Microsoft's own default .editorconfig/conventions
            {"ruby",       {.useTabs = false, .width = 2}}, // community Ruby Style Guide / RuboCop default
            {"r",          {.useTabs = false, .width = 2}}, // tidyverse style guide
            {"make",       {.useTabs = true,  .width = 4}}, // GNU Make REQUIRES a literal tab to introduce a recipe line -- a hard syntax rule, not a style preference (same shape as YAML's ban, inverted); width here is display-only

            // -- common ecosystem/style-guide convention, no single canonical formatter --
            {"html",       {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"xml",        {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"css",        {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"bash",       {.useTabs = false, .width = 2}}, // Google Shell Style Guide, the closest thing to a canonical shell convention
            {"fish",       {.useTabs = false, .width = 2}}, // no canonical style guide of its own; follows the same shell convention as bash
            {"lua",        {.useTabs = false, .width = 2}}, // most common community convention; no widely-adopted canonical formatter
            {"toml",       {.useTabs = false, .width = 2}}, // common convention (Cargo.toml-style)
            {"cmake",      {.useTabs = false, .width = 2}}, // cmake-format's own default
            {"hcl",        {.useTabs = false, .width = 2}}, // terraform fmt's canonical output
            {"nix",        {.useTabs = false, .width = 2}}, // nixfmt / RFC 166 convention
            {"clojure",    {.useTabs = false, .width = 2}}, // Lisp community convention (Emacs lisp-indent-function default)
            {"janet",      {.useTabs = false, .width = 2}}, // Lisp community convention
            {"jank",       {.useTabs = false, .width = 2}}, // same Lisp family as clojure (jank's own grammar IS clojure's)

            // -- genuinely ambiguous: no single canonical convention exists, picked as the most common cross-ecosystem default and flagged as such --
            {"java",       {.useTabs = false, .width = 4}}, // most common convention in practice (Google Java Style uses 2; Oracle/IntelliJ-default/Android use 4) -- judgment call
            {"c",          {.useTabs = false, .width = 4}}, // no single canonical convention (LLVM/Google clang-format style is 2 -- confirmed via `clang-format --style=llvm --dump-config`); 4 is the more common cross-ecosystem convention and matches this repo's own .clang-format -- judgment call, most likely setting to override
            {"cpp",        {.useTabs = false, .width = 4}}, // same reasoning as c -- judgment call
            {"sql",        {.useTabs = false, .width = 4}}, // common convention -- judgment call
        };
        // clang-format on
        return table;
    }

} // namespace

std::optional<IndentStyle> BuiltinIndentStyleForLanguage(std::string_view languageKey) {
    const auto& table = BuiltinTable();
    if (const auto it = table.find(languageKey); it != table.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace ned::editor
