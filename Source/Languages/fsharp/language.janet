# .fsi signature files parse with this grammar well enough; the repository's
# separate fsharp_signature grammar is not bundled.
{:name "fsharp"
 :extensions [".fs" ".fsx" ".fsi"]
 :injection-aliases ["fs" "f#"]
 :preserve-indent true
 :line-comment "//"
 :lsp-root-markers [".fsproj" "paket.dependencies"]
}
