{:name "commonlisp"
 :extensions [".lisp" ".cl" ".asd" ".lsp"]
 :injection-aliases ["lisp" "common-lisp"]
 :line-comment ";"
 :lsp-root-markers ["*.asd"]
 :auto-pairs :lisp
 :import-resolution {:extensions ["lisp" "cl"]}
 :not-applicable {:continuation "every form is delimited; a line continues its enclosing form"
                  :injections   "nothing in it is written in another language"}
}
