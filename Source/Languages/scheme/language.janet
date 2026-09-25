{:name "scheme"
 :extensions [".scm" ".ss" ".sld" ".sls"]
 :injection-aliases ["scm"]
 :line-comment ";"
 :auto-pairs :lisp
 :import-resolution {:extensions ["scm" "ss" "sld"]}
 :not-applicable {:continuation "every form is delimited; a line continues its enclosing form"
                  :injections   "nothing in it is written in another language"
                  :lsp-root     "no project file of its own; the root falls through to the project's"}
}
