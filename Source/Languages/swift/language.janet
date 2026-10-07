{:name "swift"
 :icon "" # nf-seti-swift
 :icon-color "#e37933"
 :extensions [".swift"]
 :line-comment "//"
 :lsp-root-markers ["Package.swift"]
 :import-resolution {:extensions ["swift"] :source-roots ["Sources"] :package-directories true}
 :signature-template "func __ned_sig({}) {}"
}
