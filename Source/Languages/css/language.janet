# No :line-comment -- CSS only has block comments (/* */).

{:name "css"
 :extensions [".css"]
 :block-comment ["/*" "*/"]
 # Stylesheet spellings: `#f0a` is a colour here rather than the start of a
 # comment, and `tomato` a colour rather than an identifier
 # (Editor/ColorLiteral.h).
 :color-literals [:short-hex :named]
 :import-resolution {:extensions ["css"]}
 :snippets
 {
   "media"
   "@media (${1:min-width}: ${2:768px}) {\n    $0\n}"
   "flexcenter"
   "display: flex;\nalign-items: center;\njustify-content: center;$0"
   "keyframes"
   "@keyframes ${1:name} {\n    from {\n        $2\n    }\n    to {\n        $0\n    }\n}"}
 :not-applicable {:locals     "custom properties are document-global; nothing is scoped"
                  :injections "nothing in it is written in another language"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
