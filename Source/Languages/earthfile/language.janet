{:name "earthfile"
 :extensions [".earth"]
 :filenames ["Earthfile"]
 :line-comment "#"
 :not-applicable {:continuation "lines continue with `\\` (`:line-continuation`)"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "FUNCTION arguments pass by name (`--arg=value`); order carries nothing"
                  :lsp-root     "no project file of its own; the root falls through to the project's"
                  :style        "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
