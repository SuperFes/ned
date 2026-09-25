{:name "commonlisp"
 :extensions [".lisp" ".cl" ".asd" ".lsp"]
 :injection-aliases ["lisp" "common-lisp"]
 :line-comment ";"
 :lsp-root-markers ["*.asd"]
 :auto-pairs :lisp
 :import-resolution {:extensions ["lisp" "cl"]}
}
