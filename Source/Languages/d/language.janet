{:name "d"
 :extensions [".d" ".di" ".dd"]
 :line-comment "//"
 :lsp-root-markers ["dub.json" "dub.sdl"]
 # The __EOF__ marker, the class Perl's __END__ gets as @preproc.
 :capture-classes {"text" :keyword}
 :signature-template "void __ned_sig({}) {}"
}
