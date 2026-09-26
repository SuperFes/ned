{:name "vala"
 :extensions [".vala" ".vapi"]
 :line-comment "//"
 :signature-template "void __ned_sig ({}) {}"
 :lsp-root-markers ["meson.build" "compile_commands.json"]
 :not-applicable {:injections "nothing in it is written in another language"
                  :imports    "a using directive names a namespace any number of files add to, so it names no one file"}
}
