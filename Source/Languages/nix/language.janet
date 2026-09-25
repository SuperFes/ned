{:name "nix"
 :extensions [".nix"]
 :line-comment "#"
 :lsp-root-markers ["flake.nix"]
 :first-pattern-wins true
 :import-resolution {:extensions ["nix"] :index-basenames ["default"]}
}
