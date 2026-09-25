# ~/.gitconfig, .gitmodules, a repository's .git/config and the XDG
# ~/.config/git/config share the syntax.
{:name "gitconfig"
 :extensions [".gitconfig"]
 :filenames [".gitconfig" ".gitmodules" "gitconfig" ".git/config" "git/config"]
 :line-comment "#"
 :import-resolution {:home-prefix true}
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "key/value lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
