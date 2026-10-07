{:name "rescript"
 :icon "" # nf-seti-rescript
 :icon-color "#cc3e44"
 :extensions [".res" ".resi"]
 :injection-aliases ["res"]
 :line-comment "//"
 :signature-template "let __ned_sig = ({}) => ()"
 :lsp-root-markers ["rescript.json" "bsconfig.json"]
 :import-resolution {:extensions ["res" "resi"] :flat-modules true}
}
