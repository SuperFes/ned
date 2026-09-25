{:name "racket"
 :extensions [".rkt" ".rktl" ".rktd"]
 :injection-aliases ["rkt"]
 :line-comment ";"
 :auto-pairs :lisp
 :import-resolution {:extensions ["rkt"]}
 :queries {:locals ["racket/locals.janet"]}
 :lsp-root-markers ["info.rkt"]
}
