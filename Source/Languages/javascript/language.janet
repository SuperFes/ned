{:name "javascript"
 :extensions [".js" ".mjs" ".cjs"]
 :line-comment "//"
 :lsp-root-markers ["package.json" "jsconfig.json"]
 :import-resolution {:extensions ["js" "jsx" "mjs" "cjs"] :index-basenames ["index"] :search-package-dirs true}
 :injection-aliases ["js"]
 :snippets
 {
   "func"
   "function ${1:name}(${2:args}) {\n    $0\n}"
   "arrow"
   "const ${1:name} = (${2:args}) => {\n    $0\n};"
   "for"
   "for (let ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n    $0\n}"
   "forof"
   "for (const ${1:item} of ${2:iterable}) {\n    $0\n}"
   "if"
   "if (${1:condition}) {\n    $0\n}"
   "ifelse"
   "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"
   "try"
   "try {\n    $1\n} catch (${2:error}) {\n    $0\n}"
   "class"
   "class ${1:Name} {\n    constructor(${2:args}) {\n        $0\n    }\n}"
   "log"
   "console.log(${1:value});$0"
   "import"
   "import ${1:module} from '${2:package}';$0"}
}
