#include "BundledLanguages.h"

#include "Languages/Escapes.h"
#include "TreeSitter/Queries.h"

namespace ned::editor {

namespace {

    namespace q = treesitter::queries;

    // The Org/Markdown table-editing keys, mirrored between the two: S-TAB is
    // unbound globally; M-UP/M-DOWN deliberately shadow move-line-up/down
    // with a metaup/metadown that falls back to the same line move outside a
    // table. Every Meta chord gets the dual M-/ESC-prefix binding the global
    // keymap uses ("cover both real input shapes").
    std::vector<std::pair<std::string, std::string>> TableKeys(std::string_view prefix) {
        const std::string p(prefix);
        return {
            {"S-TAB", p + "-table-previous-cell"},
            {"M-UP", p + "-metaup"},
            {"ESC UP", p + "-metaup"},
            {"M-DOWN", p + "-metadown"},
            {"ESC DOWN", p + "-metadown"},
            {"M-S-DOWN", p + "-table-insert-row"},
            {"ESC S-DOWN", p + "-table-insert-row"},
            {"M-S-UP", p + "-table-kill-row"},
            {"ESC S-UP", p + "-table-kill-row"},
            {"M-S-RIGHT", p + "-table-insert-column"},
            {"ESC S-RIGHT", p + "-table-insert-column"},
            {"M-S-LEFT", p + "-table-delete-column"},
            {"ESC S-LEFT", p + "-table-delete-column"},
            {"M-LEFT", p + "-table-move-column-left"},
            {"ESC LEFT", p + "-table-move-column-left"},
            {"M-RIGHT", p + "-table-move-column-right"},
            {"ESC RIGHT", p + "-table-move-column-right"},
        };
    }

    std::vector<std::pair<std::string, std::string>> Concat(std::vector<std::pair<std::string, std::string>> a,
                                                            std::vector<std::pair<std::string, std::string>> b) {
        a.insert(a.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
        return a;
    }

    std::vector<LanguageDefinition> Build() {
        languages::RegisterBundledEscapes();
        std::vector<LanguageDefinition> out;

        out.push_back({.name = "fundamental", .grammarless = true});

        out.push_back({.name              = "janet",
                       .extensions        = {".janet"},
                       .lineCommentPrefix = ";",               // Lisp-family convention
                       .autoPairs         = AutoPairSet::Lisp, // '(...) is the reader's quote macro, not a paired delimiter
                       .queries           = {.highlights = q::kJanet, .imports = q::kJanetImports, .indents = q::kJanetIndents, .locals = q::kJanetLocals}});

        // No line comment: JSON has no comment syntax at all, real or
        // otherwise; toggle-line-comment correctly reports nothing configured
        // rather than inserting something that would make the file invalid.
        out.push_back({.name = "json", .extensions = {".json"}, .queries = {.highlights = q::kJson}});

        out.push_back({.name              = "c",
                       .extensions        = {".c", ".h"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kC, .imports = q::kCImports, .tags = q::kCTags, .indents = q::kCIndents, .locals = q::kCLocals},
                       .escapes           = {"c.line-inspect"}});

        out.push_back({.name              = "cpp",
                       .extensions        = {".cpp", ".cc", ".cxx", ".hpp", ".hh"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kCpp, .imports = q::kCImports, .tags = q::kCppTags, .tests = q::kCppTests, .indents = q::kCppIndents, .locals = q::kCppLocals},
                       .escapes           = {"c.line-inspect", "cpp.test-body"}});

        out.push_back({.name              = "php",
                       .extensions        = {".php", ".phtml"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kPhp, .imports = q::kPhpImports, .tags = q::kPhpTags, .tests = q::kPhpTests, .locals = q::kPhpLocals}});

        out.push_back({.name              = "javascript",
                       .extensions        = {".js", ".mjs", ".cjs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kJavaScript, .imports = q::kJavaScriptImports, .tags = q::kJavaScriptTags, .tests = q::kJavaScriptTests, .indents = q::kJavaScriptIndents, .locals = q::kJavaScriptLocals}});

        out.push_back({.name              = "typescript",
                       .extensions        = {".ts", ".mts", ".cts"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kTypeScript, .imports = q::kTypeScriptImports, .tags = q::kTypeScriptTags, .tests = q::kTypeScriptTests, .indents = q::kTypeScriptIndents, .locals = q::kTypeScriptLocals}});

        // Every query is TypeScript's except indents: JSX needs its own rules
        // and only the tsx dialect's parser knows the node types they name
        // (see tsx-indents.scm).
        out.push_back({.name              = "tsx",
                       .extensions        = {".tsx"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kTypeScript, .imports = q::kTypeScriptImports, .tags = q::kTypeScriptTags, .tests = q::kTypeScriptTests, .indents = q::kTsxIndents, .locals = q::kTypeScriptLocals}});

        // No line comment: HTML/CSS/XML have block comments only. HTML's
        // <script>/<style> regions are synced to their own language servers.
        out.push_back({.name              = "html",
                       .extensions        = {".html", ".htm"},
                       .embeddedDocuments = true,
                       .queries           = {.highlights = q::kHtml, .indents = q::kHtmlIndents, .injections = q::kHtmlInjections}});

        out.push_back({.name = "css", .extensions = {".css"}, .queries = {.highlights = q::kCss, .imports = q::kCssImports}});

        out.push_back({.name              = "python",
                       .extensions        = {".py", ".pyw"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = q::kPython, .imports = q::kPythonImports, .tags = q::kPythonTags, .tests = q::kPythonTests, .indents = q::kPythonIndents, .locals = q::kPythonLocals}});

        out.push_back({.name              = "bash",
                       .extensions        = {".sh", ".bash"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = q::kBash, .imports = q::kBashImports, .indents = q::kBashIndents, .locals = q::kBashLocals}});

        out.push_back({.name              = "fish",
                       .extensions        = {".fish"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = q::kFish, .indents = q::kFishIndents, .locals = q::kFishLocals}});

        out.push_back({.name       = "xml",
                       .extensions = {".xml", ".xsd", ".xsl", ".xslt", ".svg"},
                       .queries    = {.highlights = q::kXml, .indents = q::kXmlIndents}});

        out.push_back({.name              = "rust",
                       .extensions        = {".rs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kRust, .imports = q::kRustImports, .tags = q::kRustTags, .tests = q::kRustTests, .indents = q::kRustIndents, .locals = q::kRustLocals}});

        out.push_back({.name              = "go",
                       .extensions        = {".go"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kGo, .tags = q::kGoTags, .tests = q::kGoTests, .indents = q::kGoIndents, .locals = q::kGoLocals}});

        out.push_back({.name              = "csharp",
                       .extensions        = {".cs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kCSharp, .tags = q::kCSharpTags, .tests = q::kCSharpTests, .indents = q::kCSharpIndents, .locals = q::kCSharpLocals}});

        out.push_back({.name              = "java",
                       .extensions        = {".java"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kJava, .tags = q::kJavaTags, .tests = q::kJavaTests, .indents = q::kJavaIndents, .locals = q::kJavaLocals}});

        // .kts is a Kotlin *script* (a Gradle build file, most often) -- the
        // same grammar and the same mode, no separate dialect.
        out.push_back({.name              = "kotlin",
                       .extensions        = {".kt", ".kts"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = q::kKotlin, .tags = q::kKotlinTags, .tests = q::kKotlinTests, .indents = q::kKotlinIndents, .locals = q::kKotlinLocals}});

        out.push_back({.name              = "yaml",
                       .extensions        = {".yaml", ".yml"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = q::kYaml, .indents = q::kYamlIndents}});

        out.push_back({.name = "toml", .extensions = {".toml"}, .lineCommentPrefix = "#", .queries = {.highlights = q::kToml}});

        // .edn is data, not code, but it's read with Clojure's own reader
        // syntax -- same reasoning as .json. .bb is babashka, a Clojure
        // dialect like jank but with no extra syntax of its own.
        out.push_back({.name              = "clojure",
                       .extensions        = {".clj", ".cljs", ".cljc", ".edn", ".bb"},
                       .lineCommentPrefix = ";",
                       .autoPairs         = AutoPairSet::Lisp,
                       .queries           = {.highlights = q::kClojure, .imports = q::kClojureImports, .indents = q::kClojureIndents, .locals = q::kClojureLocals}});

        // jank is a Clojure dialect with no grammar of its own: Clojure's
        // grammar and queries under a distinct name, so the mode line reads
        // (jank-mode) in a .jank buffer.
        out.push_back({.name              = "jank",
                       .grammar           = "clojure",
                       .extensions        = {".jank"},
                       .lineCommentPrefix = ";",
                       .autoPairs         = AutoPairSet::Lisp,
                       .queries           = {.highlights = q::kClojure, .imports = q::kClojureImports, .indents = q::kClojureIndents, .locals = q::kClojureLocals}});

        // No line comment: Markdown has no comment-line convention of its
        // own. Prose wraps (WrapOverrides.h is the per-file override).
        // "punctuation.special" -- list markers, thematic breaks, heading and
        // blockquote markers -- gets the dimmed MarkupMarker treatment here
        // and stays Punctuation in every other grammar. Fenced code blocks
        // are highlighted as their language but not synced to its server.
        out.push_back({.name           = "markdown",
                       .extensions     = {".md", ".markdown"},
                       .wrapLines      = true,
                       .keymap         = Concat({{"TAB", "markdown-table-align"}}, TableKeys("markdown")),
                       .captureClasses = {{"punctuation.special", SyntaxClass::MarkupMarker}},
                       .queries        = {.highlights = q::kMarkdown, .injections = q::kMarkdownInjections},
                       .escapes        = {"markdown.highlight", "markdown.indent", "markdown.symbols"}});

        // Real Org's own bindings, with three deliberate mode-over-global
        // shadows (C-c C-p over toggle-project-sidebar, C-c C-o over
        // find-scratch, C-c C-s/C-c C-d over project-search/create-directory)
        // -- a mode layer overriding the global layer per buffer is exactly
        // what KeymapStack exists for. Clock-in/out/report use plain letters
        // under C-c C-x rather than real Org's C-i/C-o: Ctrl-I is
        // byte-identical to Tab over a raw terminal. "org" is ned's own forked
        // grammar; its highlighting needs the org.* escapes (see
        // Languages/Org.cpp). "#" is org-comment-string's default.
        out.push_back({.name              = "org",
                       .extensions        = {".org"},
                       .lineCommentPrefix = "#",
                       .wrapLines         = true,
                       .keymap            = Concat({{"C-c C-t", "org-cycle-todo"},
                                                    {"C-c C-p", "org-cycle-priority"},
                                                    {"C-c C-c", "org-toggle-checkbox"},
                                                    {"TAB", "org-cycle"},
                                                    {"C-c C-q", "org-set-tags"},
                                                    {"C-c C-x p", "org-set-property"},
                                                    {"C-c C-x d", "org-delete-property"},
                                                    {"C-c C-x i", "org-clock-in"},
                                                    {"C-c C-x o", "org-clock-out"},
                                                    {"C-c C-x r", "org-clock-report"},
                                                    {"C-c C-s", "org-schedule"},
                                                    {"C-c C-d", "org-deadline"},
                                                    {"C-c C-o", "open-link-at-point"}},
                                                   Concat(TableKeys("org"), {{"C-c -", "org-table-insert-hline"}})),
                       .queries           = {.highlights = q::kOrg, .injections = q::kOrgInjections},
                       .escapes           = {"org.highlight", "org.indent", "org.symbols"}});

        return out;
    }

} // namespace

const std::vector<LanguageDefinition>& BundledLanguages() {
    static const std::vector<LanguageDefinition> kLanguages = Build();
    return kLanguages;
}

const LanguageDefinition* BundledLanguage(std::string_view name) {
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.name == name) {
            return &definition;
        }
    }
    return nullptr;
}

} // namespace ned::editor
