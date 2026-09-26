{:name "vhdl"
 :extensions [".vhd" ".vhdl"]
 :line-comment "--"
 :signature-template "package __ned_p is procedure __ned_sig({}); end package;"
 :lsp-root-markers ["vhdl_ls.toml"]
 :import-resolution {:extensions ["vhd" "vhdl"] :flat-modules true :declared-names true}
 :not-applicable {:style "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
