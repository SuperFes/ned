{:name "gdscript"
 :extensions [".gd"]
 :injection-aliases ["gd"]
 :line-comment "#"
 :lsp-root-markers ["project.godot"]
 :signature-template "func __ned_sig({}):\n\tpass"
 :import-resolution {:extensions ["gd"] :root-prefixes [["res://" ""]]}
 :not-applicable {:continuation "a line continues only inside brackets or after `\\`"
                  :injections   "nothing in it is written in another language"}
}
