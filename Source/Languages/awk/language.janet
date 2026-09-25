# awk: imported 2026-09-21 from https://github.com/Beaglefoot/tree-sitter-awk (v0.7.2)
# Admission facts (Docs/LanguageCoverage.md): generated ABI 14, scanner 248
# lines (ported to Source/Editor/Languages/Scanners/AwkScanner.cpp), corpus 7
# files.

{:name "awk"
 :extensions [".awk" ".gawk" ".mawk"]
 :injection-aliases ["gawk"]
 :line-comment "#"
 :line-continuation "\\"
 :capture-classes {"regexp" :string}
 :first-pattern-wins true
 :import-resolution {:extensions ["awk"]}
 :not-applicable {:injections "nothing in it is written in another language"
                  :tests      "no test framework runs tests written in it"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
