{:name "latex"
 :extensions [".tex" ".sty" ".cls" ".ltx" ".dtx" ".ins"]
 :injection-aliases ["tex"]
 :line-comment "%"
 :wrap-lines true
 :lsp-root-markers [".latexmkrc" "latexmkrc" "Tectonic.toml"]
 :import-resolution {:extensions ["tex" "bib"]}
 :not-applicable {:continuation "markup; nothing continues across lines"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "macro arguments are positional `#1`; there is no parameter list to rewrite"}
}
