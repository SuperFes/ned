# desktop: imported 2026-09-21 from https://github.com/ValdezFOmar/tree-sitter-desktop (v1.1.1)
# Admission facts (Docs/LanguageCoverage.md): generated ABI 15, scanner 0 lines, corpus 1 file.

{:name "desktop"
 :extensions [".desktop" ".directory"]
 :line-comment "#"

 # Upstream spells a group header @markup.heading, which falls through to
 # Default everywhere but XML. A .desktop group is ini's section, so it gets
 # the class ini/toml/systemd give theirs.
 :capture-classes {"markup.heading" :type}
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "key/value lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
