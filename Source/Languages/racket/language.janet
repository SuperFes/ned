{:name "racket"
 :extensions [".rkt" ".rktl" ".rktd"]
 :injection-aliases ["rkt"]
 :line-comment ";"
 :auto-pairs :lisp
 :import-resolution {:extensions ["rkt"]}
 :queries {:locals ["racket/locals.janet"]}
 :lsp-root-markers ["info.rkt"]
 :not-applicable {:continuation "every form is delimited; a line continues its enclosing form"
                  :injections   "nothing in it is written in another language"}
}
