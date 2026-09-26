{:name "ada"
 :extensions [".ads" ".adb" ".ada"]
 :injection-aliases ["adb" "ads"]
 :line-comment "--"
 :lsp-root-markers ["alire.toml"]
 :signature-template "procedure Ned_Sig ({}) is begin null; end Ned_Sig;"
 :not-applicable {:injections "nothing in it is written in another language"
                  :style      "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
