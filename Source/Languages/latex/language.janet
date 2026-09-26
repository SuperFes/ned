{:name "latex"
 :extensions [".tex" ".sty" ".cls" ".ltx" ".dtx" ".ins"]
 :injection-aliases ["tex"]
 :line-comment "%"
 :wrap-lines true
 :lsp-root-markers [".latexmkrc" "latexmkrc" "Tectonic.toml"]
 :import-resolution {:extensions ["tex" "bib"]}
 :not-applicable {:continuation "markup; nothing continues across lines"
                  :locals       "labels and macros are global to a document that spans `\\input` files; arguments are positional `#1`"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "macro arguments are positional `#1`; there is no parameter list to rewrite"
                  :format       "markup: no functions, control flow or definitions for the formatter's rules to place"}
}
