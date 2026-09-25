{:name "fennel"
 :extensions [".fnl"]
 :injection-aliases ["fnl"]
 :line-comment ";"
 :auto-pairs :lisp
 :import-resolution {:extensions ["fnl"] :index-basenames ["init"] :source-roots ["fnl" "src"]}
 :not-applicable {:continuation "every form is delimited; a line continues its enclosing form"
                  :injections   "nothing in it is written in another language"
                  :lsp-root     "no project file of its own; the root falls through to the project's"}
}
