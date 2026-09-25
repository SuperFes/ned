# .v belongs to Verilog (far more files in the wild) unless the file uses
# forms Verilog never has: `fn`/`struct` declarations, a bare `module name`
# line. libmagic cannot tell the two apart.
{:name "v"
 :extensions [".vsh" ".vv"]
 :injection-aliases ["vlang"]
 :shared-extensions [".v"]
 :content-pattern "^\\s*(pub\\s+)?(fn|struct)\\s|^module\\s+\\w+\\s*$"
 :filenames ["v.mod"]
 :line-comment "//"
 :lsp-root-markers ["v.mod"]
 :signature-template "fn __ned_sig({}) {}"
 :braces-on-header-line true
 :import-resolution {:extensions ["v"] :source-roots ["" "modules"] :package-directories true}
}
