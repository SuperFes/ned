# nginx: imported 2026-09-21 from https://github.com/opa-oz/tree-sitter-nginx (v1.0.1)
# Admission facts (Docs/LanguageCoverage.md): generated ABI 15, scanner 151
# lines (ported to Source/Editor/Languages/Scanners/NginxScanner.cpp), corpus
# 4 files.
#
# nginx has no extension convention of its own -- the files are nginx.conf and
# whatever conf.d/sites-available holds -- so only the unambiguous basenames
# are claimed; a bare *.conf belongs to nobody in particular.

{:name "nginx"
 :extensions [".nginx"]
 :filenames ["nginx.conf" "mime.types"]
 :line-comment "#"
 :not-applicable {:indents           "the grammar's delimited bodies are its whole indent structure"
                  :locals            "variables are request-global"
                  :tests             "no test framework runs tests written in it"
                  :signatures        "no user-defined functions with parameter lists"
                  :lsp-root          "no project file of its own; the root falls through to the project's"
                  :import-resolution "its specifiers are relative paths the default resolution handles"}
}
