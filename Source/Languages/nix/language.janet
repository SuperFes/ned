{:name "nix"
 :extensions [".nix"]
 :line-comment "#"
 :lsp-root-markers ["flake.nix"]
 :first-pattern-wins true
 :import-resolution {:extensions ["nix"] :index-basenames ["default"]}
 :not-applicable {:tests      "no test framework runs tests written in it"
                  :signatures "arguments pass by name in an attrset; order carries nothing"}
}
