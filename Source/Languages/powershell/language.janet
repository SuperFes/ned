{:name "powershell"
 :extensions [".ps1" ".psm1" ".psd1"]
 :injection-aliases ["ps1" "pwsh"]
 :line-comment "#"
 :signature-template "function __ned_sig({}) {}"
 :import-resolution {:extensions ["ps1" "psm1"]}
 :lsp-root-markers ["*.psd1"]
 :not-applicable {:injections "nothing in it is written in another language"}
}
