{:name "nix"
 :extensions [".nix"]
 :line-comment "#"
 :lsp-root-markers ["flake.nix"]
 :first-pattern-wins true
 :import-resolution {:extensions ["nix"] :index-basenames ["default"]}
 :not-applicable {:tests      "no test framework runs tests written in it"
                  :signatures "arguments pass by name in an attrset; order carries nothing"
                  :format     "a file is one expression: no statements, definitions or bodies of the kinds the formatter's rules place"}
}
