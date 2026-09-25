# wgsl: imported 2026-09-21 from https://github.com/tree-sitter-grammars/tree-sitter-wgsl-bevy (v0.1.4)
# Admission facts (Docs/LanguageCoverage.md): generated ABI 14, scanner 73
# lines (ported to Source/Editor/Languages/Scanners/WgslScanner.cpp), corpus 2
# files.
#
# The bevy dialect is plain WGSL plus its own preprocessor directives
# (#import, #ifdef), so it parses stock shaders as well as Bevy's own.
# grammar.janet adds what v0.1.4 predates in the spec: `const`, `alias` and
# `const_assert`.

{:name "wgsl"
 :extensions [".wgsl"]
 :line-comment "//"
 :signature-template "fn __ned_sig({}) {}"
}
