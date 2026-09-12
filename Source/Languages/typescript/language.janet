{:name "typescript"
 :extensions [".ts" ".mts" ".cts"]
 :line-comment "//"

 # Three files, and the javascript one is not optional --
 # tree-sitter-typescript's tags.scm is a delta on javascript's and carries
 # no class_declaration/function_declaration of its own, so embedding it
 # alone leaves every class and function with no symbol marker at all.
 :queries {:tags ["javascript/upstream/tags.janet"
           "typescript/upstream/tags.janet"
           "typescript/tags.janet"]}
 :lsp-root-markers ["package.json" "tsconfig.json"]
 :import-resolution {:extensions ["ts" "tsx" "js" "jsx" "mjs" "cjs"] :index-basenames ["index"] :search-package-dirs true}
 :injection-aliases ["ts"]
 :snippets
 {
   "func"
   "function ${1:name}(${2:args}): ${3:void} {\n    $0\n}"
   "arrow"
   "const ${1:name} = (${2:args}): ${3:void} => {\n    $0\n};"
   "interface"
   "interface ${1:Name} {\n    $0\n}"
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
