{:name "make"
 :extensions [".mk" ".mak"]
 :injection-aliases ["makefile" "mk"]
 :filenames ["Makefile" "makefile" "GNUmakefile"]
 :line-comment "#"
 # The message text of $(error ...), $(warning ...) and $(info ...).
 :capture-classes {"text" :string}
 :not-applicable {:indents           "recipes are tab-prefixed; conditionals don't indent"
                  :locals            "variables are global across included makefiles"
                  :tests             "no test framework runs tests written in it"
                  :signatures        "`$(call)` arguments are positional `$(1)`; there is no parameter list"
                  :lsp-root          "no project file of its own; the root falls through to the project's"
                  :import-resolution "its specifiers are relative paths the default resolution handles"}
}
