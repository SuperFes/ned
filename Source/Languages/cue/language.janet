# cue: imported 2026-09-21 from https://github.com/eonpatapon/tree-sitter-cue (v0.1.0)
# Admission facts (Docs/LanguageCoverage.md): generated ABI 14, scanner 225
# lines (ported to Source/Editor/Languages/Scanners/CueScanner.cpp), corpus 13
# files.

{:name "cue"
 :extensions [".cue"]
 :line-comment "//"
 :import-resolution {:extensions ["cue"] :cue-modules true :package-directories true}
 :lsp-root-markers ["cue.mod"]
 :first-pattern-wins true
 :queries {:highlights ["cue/highlights.janet"
                        "cue/upstream/highlights.janet"]}
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "configuration values: no functions, statements or bodies for the formatter's rules to place"}
}
