{:name "fish"
 :extensions [".fish"]
 :line-comment "#"
 :line-continuation "\\"
 :import-resolution {:extensions ["fish"]}
 :not-applicable {:injections "nothing in it is written in another language"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :signatures "functions read `$argv`; `--argument-names` names its positions among the header's other options"}
}
