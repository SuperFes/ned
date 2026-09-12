{:name "janet"
 :extensions [".janet"]

 # Lisp-family convention.
 :line-comment ";"

 # '(...) is the reader's quote macro, not a paired delimiter.
 :auto-pairs :lisp
 :import-resolution {:extensions ["janet"]}
 :snippets
 {
   "defn"
   "(defn ${1:name} [${2:args}]\n  $0)"
   "fn"
   "(fn [${1:args}]\n  $0)"
   "let"
   "(let [${1:name} ${2:value}]\n  $0)"
   "for"
   "(for [${1:i} :range [0 ${2:n}]]\n  $0)"
   "each"
   "(each ${1:item} ${2:coll}\n  $0)"
   "var"
   "(var ${1:name} ${2:value})$0"}
}
