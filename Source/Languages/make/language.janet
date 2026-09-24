{:name "make"
 :extensions [".mk" ".mak"]
 :filenames ["Makefile" "makefile" "GNUmakefile"]
 :line-comment "#"
 # The message text of $(error ...), $(warning ...) and $(info ...).
 :capture-classes {"text" :string}
}
