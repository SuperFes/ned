# jsonnet: imported 2026-09-21 from https://github.com/sourcegraph/tree-sitter-jsonnet
# (ddd075f1939aed8147b7aa67f042eda3fce22790). Admission facts
# (Docs/LanguageCoverage.md): generated ABI 14, scanner 176 lines (ported to
# Source/Editor/Languages/Scanners/JsonnetScanner.cpp), corpus 7 files.

{:name "jsonnet"
 :extensions [".jsonnet" ".libsonnet"]
 :injection-aliases ["libsonnet"]
 :line-comment "//"
 :lsp-root-markers ["jsonnetfile.json"]
 :capture-classes {"define" :variable}
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :import-resolution {:extensions ["jsonnet" "libsonnet"]}
 :signature-template "local __ned_sig({}) = null; null"
}
