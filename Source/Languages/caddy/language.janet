# caddy: imported 2026-09-21 from https://github.com/caddyserver/tree-sitter-caddyfile
# (4ef0479e11161ef4d1b4a89ae966eb1f5f11d764; Caddy's own grammar). Admission
# facts (Docs/LanguageCoverage.md): generated ABI 15, scanner 251 lines (ported
# to Source/Editor/Languages/Scanners/CaddyScanner.cpp), corpus 14 files.
#
# The file is "Caddyfile" by convention, with no extension; the extension
# spellings upstream declares are claimed too.

{:name "caddy"
 :extensions [".caddyfile" ".Caddyfile"]
 :injection-aliases ["caddyfile"]
 :filenames ["Caddyfile" "caddyfile"]
 :line-comment "#"
 :not-applicable {:indents           "the grammar's delimited bodies are its whole indent structure"
                  :tests             "no test framework runs tests written in it"
                  :signatures        "no user-defined functions with parameter lists"
                  :lsp-root          "no project file of its own; the root falls through to the project's"
                  :import-resolution "its specifiers are relative paths the default resolution handles"}
}
