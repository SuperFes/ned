{:name "make"
 :extensions [".mk" ".mak"]
 :injection-aliases ["makefile" "mk"]
 :filenames ["Makefile" "makefile" "GNUmakefile"]
 :line-comment "#"
 # The message text of $(error ...), $(warning ...) and $(info ...).
 :capture-classes {"text" :string}
}
