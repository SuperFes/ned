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
            {"gdscript",   {.useTabs = true,  .width = 4}}, // Godot's GDScript style guide: tabs, and the editor's own default
            {"nim",        {.useTabs = false, .width = 2}}, // NEP-1; the compiler rejects tab indentation outright
            {"ocaml",      {.useTabs = false, .width = 2}}, // ocamlformat default
            {"ocaml-interface", {.useTabs = false, .width = 2}}, // ocamlformat default
            {"pascal",     {.useTabs = false, .width = 2}}, // Delphi (Embarcadero) Object Pascal style guide
            {"scala",      {.useTabs = false, .width = 2}}, // scalafmt default, Scala style guide
            {"elixir",     {.useTabs = false, .width = 2}}, // mix format
            {"erlang",     {.useTabs = false, .width = 4}}, // erlfmt
            {"dart",       {.useTabs = false, .width = 2, .continuation = 2}}, // dart format (not configurable); continuation lines +4
            {"julia",      {.useTabs = false, .width = 4}}, // Julia style guide
            {"crystal",    {.useTabs = false, .width = 2}}, // crystal tool format
            {"ada",        {.useTabs = false, .width = 3}}, // GNAT coding style, gnatpp's default
            {"make",       {.useTabs = true,  .width = 4}}, // GNU Make REQUIRES a literal tab to introduce a recipe line -- a hard syntax rule, not a style preference (same shape as YAML's ban, inverted); width here is display-only
            {"gleam",      {.useTabs = false, .width = 2}}, // gleam format (not configurable)
            {"odin",       {.useTabs = true,  .width = 4}}, // odinfmt default, and Odin's own core library
            {"v",          {.useTabs = true,  .width = 4}}, // v fmt (not configurable)
            {"cue",        {.useTabs = true,  .width = 4}}, // cue fmt (not configurable)
            {"rescript",   {.useTabs = false, .width = 2}}, // rescript format (not configurable)
            {"purescript", {.useTabs = false, .width = 2}}, // purs-tidy default
            {"jsonnet",    {.useTabs = false, .width = 2}}, // jsonnetfmt default
            {"pkl",        {.useTabs = false, .width = 2}}, // pkl format, and Pkl's own standard library
            {"typst",      {.useTabs = false, .width = 2}}, // typstyle default
            {"proto",      {.useTabs = false, .width = 2}}, // buf format, and clang-format's proto default
            {"d",          {.useTabs = false, .width = 4}}, // D style guide (dlang.org/dstyle.html), dfmt default
            {"elm",        {.useTabs = false, .width = 4}}, // elm-format (not configurable), Elm style guide
            {"fsharp",     {.useTabs = false, .width = 4}}, // F# style guide (learn.microsoft.com), Fantomas default
            {"haskell",    {.useTabs = false, .width = 2}}, // Ormolu, the Haskell Language Server's default formatter (Fourmolu's 4 is opt-in)
            {"perl",       {.useTabs = false, .width = 4}}, // perlstyle's "4-column indent", Perl::Tidy default
            {"powershell", {.useTabs = false, .width = 4}}, // PowerShell Practice and Style guide, PSScriptAnalyzer default
            {"solidity",   {.useTabs = false, .width = 4}}, // Solidity style guide
            {"starlark",   {.useTabs = false, .width = 4}}, // buildifier (not configurable)
            {"tcl",        {.useTabs = false, .width = 4}}, // Tcl Style Guide, section 6
            {"matlab",     {.useTabs = false, .width = 4}}, // MATLAB Editor's default indent size
            {"meson",      {.useTabs = false, .width = 4}}, // meson format's indent_by default
            {"just",       {.useTabs = false, .width = 4}}, // just --fmt
            {"nu",         {.useTabs = false, .width = 4}}, // nufmt default, and Nushell's own standard library
            {"ron",        {.useTabs = false, .width = 4}}, // ron's PrettyConfig default indentor
            {"kdl",        {.useTabs = false, .width = 4}}, // kdlfmt and kdl-rs's formatter default (the spec's own examples use 2)
            {"wgsl",       {.useTabs = false, .width = 4}}, // wgslfmt and naga's WGSL writer (the W3C spec's own examples use 2)
            {"fortran",    {.useTabs = false, .width = 3}}, // fprettify and findent defaults (fortran-lang's guide leaves 2-4 to preference)
            {"verilog",    {.useTabs = false, .width = 2}}, // verible-verilog-format default, lowRISC style guide
            {"vhdl",       {.useTabs = false, .width = 2}}, // VHDL Style Guide (vsg) default, Emacs vhdl-mode
            {"caddy",      {.useTabs = true,  .width = 4}}, // caddy fmt writes a tab per level; width is display-only
            {"awk",        {.useTabs = true,  .width = 4}}, // gawk --pretty-print (the manual's own examples use 4 spaces); width is display-only
            {"asm",        {.useTabs = true,  .width = 4}}, // GCC/Clang -S output: a tab before each instruction, labels at column 0; width is display-only
            {"gitconfig",  {.useTabs = true,  .width = 4}}, // git config writes a tab before each key; width is display-only

            // -- common ecosystem/style-guide convention, no single canonical formatter --
            {"html",       {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"xml",        {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"css",        {.useTabs = false, .width = 2}}, // common web-ecosystem convention
            {"scss",       {.useTabs = false, .width = 2}}, // Prettier default, same as css
            {"svelte",     {.useTabs = false, .width = 2}}, // Prettier (prettier-plugin-svelte) default
            {"vue",        {.useTabs = false, .width = 2}}, // Prettier default
            {"astro",      {.useTabs = false, .width = 2}}, // Prettier (prettier-plugin-astro) default
            {"bash",       {.useTabs = false, .width = 2}}, // Google Shell Style Guide, the closest thing to a canonical shell convention
            {"lua",        {.useTabs = false, .width = 2}}, // most common community convention; no widely-adopted canonical formatter
            {"toml",       {.useTabs = false, .width = 2}}, // common convention (Cargo.toml-style)
            {"cmake",      {.useTabs = false, .width = 2}}, // cmake-format's own default
            {"hcl",        {.useTabs = false, .width = 2}}, // terraform fmt's canonical output
            {"nix",        {.useTabs = false, .width = 2}}, // nixfmt / RFC 166 convention
            {"clojure",    {.useTabs = false, .width = 2}}, // Lisp community convention (Emacs lisp-indent-function default)
            {"janet",      {.useTabs = false, .width = 2}}, // Lisp community convention
            {"jank",       {.useTabs = false, .width = 2}}, // same Lisp family as clojure (jank's own grammar IS clojure's)
            {"scheme",     {.useTabs = false, .width = 2}}, // Lisp community convention
            {"racket",     {.useTabs = false, .width = 2}}, // Lisp community convention
            {"commonlisp", {.useTabs = false, .width = 2}}, // Lisp community convention
            {"fennel",     {.useTabs = false, .width = 2}}, // fnlfmt
            {"fish",       {.useTabs = false, .width = 4}}, // fish_indent, shipped with fish
            {"json5",      {.useTabs = false, .width = 2}}, // same as json; the json5 README's own examples
            {"groovy",     {.useTabs = false, .width = 4}}, // npm-groovy-lint default, the Java-family convention Gradle scripts follow
            {"apacheconf", {.useTabs = false, .width = 4}}, // Apache's shipped httpd.conf
            {"nginx",      {.useTabs = false, .width = 4}}, // nginx's shipped conf/nginx.conf
            {"dockerfile", {.useTabs = false, .width = 4}}, // dockerfmt default, for continuation lines
            {"earthfile",  {.useTabs = false, .width = 4}}, // Earthly's own Earthfiles and documentation
            {"thrift",     {.useTabs = false, .width = 2}}, // Apache Thrift's tutorial.thrift (no formatter exists)
            {"rst",        {.useTabs = false, .width = 3}}, // Python devguide: "All reST files use an indentation of 3 spaces"

            // -- genuinely ambiguous: no single canonical convention exists, picked as the most common cross-ecosystem default and flagged as such --
            {"java",       {.useTabs = false, .width = 4, .continuation = 2}}, // most common convention in practice (Google Java Style uses 2; Oracle/IntelliJ-default/Android use 4) -- judgment call; continuation lines are two levels in both
            {"c",          {.useTabs = false, .width = 4}}, // no single canonical convention (LLVM/Google clang-format style is 2 -- confirmed via `clang-format --style=llvm --dump-config`); 4 is the more common cross-ecosystem convention and matches this repo's own .clang-format -- judgment call, most likely setting to override
            {"cpp",        {.useTabs = false, .width = 4}}, // same reasoning as c -- judgment call
            {"sql",        {.useTabs = false, .width = 4}}, // common convention -- judgment call
            {"cuda",       {.useTabs = false, .width = 4}}, // same reasoning as cpp; NVIDIA's cuda-samples use 4, CCCL uses 2 -- judgment call
            {"glsl",       {.useTabs = false, .width = 4}}, // no canonical source; Khronos's own examples lean 4 -- judgment call
            {"hlsl",       {.useTabs = false, .width = 4}}, // no canonical source; Microsoft's DirectX samples use 4 -- judgment call
            {"objc",       {.useTabs = false, .width = 4}}, // Xcode's default (Google's Objective-C guide uses 2) -- judgment call
            {"swift",      {.useTabs = false, .width = 4}}, // Xcode's default and the Swift book (swift-format defaults to 2) -- judgment call
            {"vala",       {.useTabs = false, .width = 4}}, // elementary's Vala style guide and vala-lint (the compiler's own sources use tabs) -- judgment call
            {"latex",      {.useTabs = true,  .width = 4}}, // latexindent's defaultIndent and TeXstudio's default (Overleaf's editor uses 4 spaces) -- judgment call; width is display-only
            {"ssh_config", {.useTabs = false, .width = 2}}, // OpenSSH's shipped ssh_config, commented `Host *` block -- judgment call
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
