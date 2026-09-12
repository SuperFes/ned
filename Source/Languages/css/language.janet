# No :line-comment -- CSS only has block comments (/* */).

{:name "css"
 :extensions [".css"]
 :import-resolution {:extensions ["css"]}
 :snippets
 {
   "media"
   "@media (${1:min-width}: ${2:768px}) {\n    $0\n}"
   "flexcenter"
   "display: flex;\nalign-items: center;\njustify-content: center;$0"
   "keyframes"
   "@keyframes ${1:name} {\n    from {\n        $2\n    }\n    to {\n        $0\n    }\n}"}
}
