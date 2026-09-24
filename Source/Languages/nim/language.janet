{:name "nim"
 :extensions [".nim" ".nims" ".nimble"]
 :line-comment "#"
 :lsp-root-markers ["nim.cfg" "config.nims"]
 # Nim identifiers can't start with or double an underscore.
 :signature-template "proc nedSig({}) = discard"
}
