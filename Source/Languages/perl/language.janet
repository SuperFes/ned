{:name "perl"
 :extensions [".pl" ".pm" ".t" ".pod"]
 :injection-aliases ["pl"]
 :line-comment "#"
 :lsp-root-markers ["cpanfile" "Makefile.PL" "dist.ini"]
 # POD documentation.
 :capture-classes {"text" :doc-comment}
 :import-resolution {:extensions ["pm" "pl"] :module-separator "::" :source-roots ["lib" "t/lib"]}
}
