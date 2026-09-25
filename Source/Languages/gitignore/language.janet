# gitignore: imported 2026-09-21 from https://github.com/shunsambongi/tree-sitter-gitignore
# (f4685bf11ac466dd278449bcfe5fd014e94aa504). Admission facts
# (Docs/LanguageCoverage.md): generated ABI 13, scanner 0 lines, corpus 1 file.
#
# Admitted despite being stale since 2022 (policy item 4): no other grammar
# for the format exists, and the format itself has not moved either.
#
# The tool-specific ignore files below are all git's syntax. `.git/info/exclude`
# is not claimed -- "exclude" is too generic a basename to take.

{:name "gitignore"
 :filenames [".gitignore" ".gitignore_global" ".ignore" ".fdignore" ".rgignore"
             ".dockerignore" ".npmignore" ".eslintignore" ".prettierignore" ".helmignore"]
 :line-comment "#"
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "pattern lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :tags       "patterns, not definitions"}
}
