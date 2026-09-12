#include "BundledLanguages.h"

#include "Languages/Escapes.h"

namespace ned::editor {

namespace {

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

    // Locals coverage is a deliberate list, not a backlog: json, yaml, toml
    // and xml have no binding construct to resolve, and html and css have
    // one whose scoping is not lexical -- a CSS custom property is scoped to
    // matching elements AND THEIR DESCENDANTS, which is DOM containment, so
    // the byte containment Editor/LocalScopes.h resolves by would produce a
    // rename that silently missed every descendant use.
    std::vector<LanguageDefinition> Build() {
        languages::RegisterBundledEscapes();
        std::vector<LanguageDefinition> out;

        out.push_back({.name = "fundamental", .grammarless = true});

        out.push_back({.name              = "janet",
                       .extensions        = {".janet"},
                       .lineCommentPrefix = ";",               // Lisp-family convention
                       .autoPairs         = AutoPairSet::Lisp, // '(...) is the reader's quote macro, not a paired delimiter
                       .queries           = {.highlights = {"janet/upstream/highlights.janet"}, .imports = {"janet/imports.janet"}, .indents = {"janet/indents.janet"}, .locals = {"janet/locals.janet"}}});

        // No line comment: JSON has no comment syntax at all, real or
        // otherwise; toggle-line-comment correctly reports nothing configured
        // rather than inserting something that would make the file invalid.
        out.push_back({.name = "json", .extensions = {".json"}, .queries = {.highlights = {"json/upstream/highlights.janet"}}});

        out.push_back({.name              = "c",
                       .extensions        = {".c", ".h"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"c/highlights.janet"}, .imports = {"c/imports.janet"}, .tags = {"c/tags.janet"}, .indents = {"c/indents.janet"}, .locals = {"c/locals.janet"}},
                       .escapes           = {"c.line-inspect"}});

        out.push_back({.name              = "cpp",
                       .extensions        = {".cpp", ".cc", ".cxx", ".hpp", ".hh"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"cpp/highlights.janet"}, .imports = {"c/imports.janet"}, .tags = {"cpp/tags.janet"}, .tests = {"cpp/tests.janet"}, .indents = {"cpp/indents.janet"}, .locals = {"cpp/locals.janet"}},
                       .escapes           = {"c.line-inspect", "cpp.test-body"}});

        out.push_back({.name              = "php",
                       .extensions        = {".php", ".phtml"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"php/upstream/highlights.janet"}, .imports = {"php/imports.janet"}, .tags = {"php/upstream/tags.janet", "php/tags.janet"}, .tests = {"php/tests.janet"}, .locals = {"php/locals.janet"}}});

        out.push_back({.name              = "javascript",
                       .extensions        = {".js", ".mjs", ".cjs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"javascript/upstream/highlights.janet"}, .imports = {"javascript/imports.janet"}, .tags = {"javascript/upstream/tags.janet"}, .tests = {"javascript/tests.janet"}, .indents = {"javascript/indents.janet"}, .locals = {"javascript/locals.janet"}}});

        out.push_back({.name              = "typescript",
                       .extensions        = {".ts", ".mts", ".cts"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"typescript/upstream/highlights.janet"}, .imports = {"typescript/imports.janet"}, .tags = {"javascript/upstream/tags.janet", "typescript/upstream/tags.janet", "typescript/tags.janet"}, .tests = {"typescript/tests.janet"}, .indents = {"typescript/indents.janet"}, .locals = {"typescript/locals.janet"}}});

        // Every query is TypeScript's except indents: JSX needs its own rules
        // and only the tsx dialect's parser knows the node types they name
        // (see tsx-indents.scm).
        out.push_back({.name              = "tsx",
                       .extensions        = {".tsx"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"typescript/upstream/highlights.janet"}, .imports = {"typescript/imports.janet"}, .tags = {"javascript/upstream/tags.janet", "typescript/upstream/tags.janet", "typescript/tags.janet"}, .tests = {"typescript/tests.janet"}, .indents = {"tsx/indents.janet"}, .locals = {"typescript/locals.janet"}}});

        // No line comment: HTML/CSS/XML have block comments only. HTML's
        // <script>/<style> regions are synced to their own language servers.
        out.push_back({.name              = "html",
                       .extensions        = {".html", ".htm"},
                       .embeddedDocuments = true,
                       .queries           = {.highlights = {"html/upstream/highlights.janet"}, .indents = {"html/indents.janet"}, .injections = {"html/upstream/injections.janet"}}});

        out.push_back({.name = "css", .extensions = {".css"}, .queries = {.highlights = {"css/upstream/highlights.janet"}, .imports = {"css/imports.janet"}}});

        out.push_back({.name              = "python",
                       .extensions        = {".py", ".pyw"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = {"python/upstream/highlights.janet"}, .imports = {"python/imports.janet"}, .tags = {"python/upstream/tags.janet"}, .tests = {"python/tests.janet"}, .indents = {"python/indents.janet"}, .locals = {"python/locals.janet"}}});

        out.push_back({.name              = "bash",
                       .extensions        = {".sh", ".bash"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = {"bash/upstream/highlights.janet"}, .imports = {"bash/imports.janet"}, .indents = {"bash/indents.janet"}, .locals = {"bash/locals.janet"}}});

        out.push_back({.name              = "fish",
                       .extensions        = {".fish"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = {"fish/upstream/highlights.janet"}, .indents = {"fish/indents.janet"}, .locals = {"fish/locals.janet"}}});

        out.push_back({.name       = "xml",
                       .extensions = {".xml", ".xsd", ".xsl", ".xslt", ".svg"},
                       .queries    = {.highlights = {"xml/upstream/highlights.janet"}, .indents = {"xml/indents.janet"}}});

        out.push_back({.name              = "rust",
                       .extensions        = {".rs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"rust/upstream/highlights.janet"}, .imports = {"rust/imports.janet"}, .tags = {"rust/upstream/tags.janet"}, .tests = {"rust/tests.janet"}, .indents = {"rust/indents.janet"}, .locals = {"rust/locals.janet"}}});

        out.push_back({.name              = "go",
                       .extensions        = {".go"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"go/upstream/highlights.janet"}, .tags = {"go/upstream/tags.janet"}, .tests = {"go/tests.janet"}, .indents = {"go/indents.janet"}, .locals = {"go/locals.janet"}}});

        out.push_back({.name              = "csharp",
                       .extensions        = {".cs"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"csharp/upstream/highlights.janet"}, .tags = {"csharp/upstream/tags.janet", "csharp/tags.janet"}, .tests = {"csharp/tests.janet"}, .indents = {"csharp/indents.janet"}, .locals = {"csharp/locals.janet"}}});

        out.push_back({.name              = "java",
                       .extensions        = {".java"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"java/upstream/highlights.janet"}, .tags = {"java/upstream/tags.janet", "java/tags.janet"}, .tests = {"java/tests.janet"}, .indents = {"java/indents.janet"}, .locals = {"java/locals.janet"}}});

        // .kts is a Kotlin *script* (a Gradle build file, most often) -- the
        // same grammar and the same mode, no separate dialect.
        out.push_back({.name              = "kotlin",
                       .extensions        = {".kt", ".kts"},
                       .lineCommentPrefix = "//",
                       .queries           = {.highlights = {"kotlin/upstream/highlights.janet"}, .tags = {"kotlin/tags.janet"}, .tests = {"kotlin/tests.janet"}, .indents = {"kotlin/indents.janet"}, .locals = {"kotlin/locals.janet"}}});

        out.push_back({.name              = "yaml",
                       .extensions        = {".yaml", ".yml"},
                       .lineCommentPrefix = "#",
                       .queries           = {.highlights = {"yaml/upstream/highlights.janet"}, .indents = {"yaml/indents.janet"}}});

        out.push_back({.name = "toml", .extensions = {".toml"}, .lineCommentPrefix = "#", .queries = {.highlights = {"toml/upstream/highlights.janet"}}});

        // .edn is data, not code, but it's read with Clojure's own reader
        // syntax -- same reasoning as .json. .bb is babashka, a Clojure
        // dialect like jank but with no extra syntax of its own.
        out.push_back({.name              = "clojure",
                       .extensions        = {".clj", ".cljs", ".cljc", ".edn", ".bb"},
                       .lineCommentPrefix = ";",
                       .autoPairs         = AutoPairSet::Lisp,
                       .queries           = {.highlights = {"clojure/highlights.janet"}, .imports = {"clojure/imports.janet"}, .indents = {"clojure/indents.janet"}, .locals = {"clojure/locals.janet"}}});

        // jank is a Clojure dialect with no grammar of its own: Clojure's
        // grammar and queries under a distinct name, so the mode line reads
        // (jank-mode) in a .jank buffer.
        out.push_back({.name              = "jank",
                       .grammar           = "clojure",
                       .extensions        = {".jank"},
                       .lineCommentPrefix = ";",
                       .autoPairs         = AutoPairSet::Lisp,
                       .queries           = {.highlights = {"clojure/highlights.janet"}, .imports = {"clojure/imports.janet"}, .indents = {"clojure/indents.janet"}, .locals = {"clojure/locals.janet"}}});

        // Not a file type: the inline grammar tree-sitter-markdown injects
        // into every paragraph, resolved by name from Injection.cpp. Its
        // highlights are upstream's plus ned's one addition (strikethrough).
        out.push_back({.name    = "markdown-inline",
                       .queries = {.highlights = {"markdown-inline/upstream/highlights.janet", "markdown-inline/highlights.janet"}}});

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
                       .queries        = {.highlights = {"markdown/upstream/highlights.janet"}, .injections = {"markdown/upstream/injections.janet"}},
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
                       .queries           = {.highlights = {"org/highlights.janet"}, .injections = {"org/upstream/injections.janet"}},
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
