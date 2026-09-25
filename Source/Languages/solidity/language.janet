{:name "solidity"
 :extensions [".sol"]
 :injection-aliases ["sol"]
 :line-comment "//"
 :lsp-root-markers ["hardhat.config.js" "hardhat.config.ts" "foundry.toml" "truffle-config.js"]
 :signature-template "contract __Ned { function __ned_sig({}) public {} }"
 :import-resolution {:extensions ["sol"] :search-package-dirs true}
 :not-applicable {:injections "nothing in it is written in another language"}
}
