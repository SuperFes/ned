# Typescript's queries except indents: JSX needs its own rules and only the
# tsx dialect's parser knows the node types they name (tsx/indents.janet).

{:name "tsx"
 :extensions [".tsx"]
 :line-comment "//"
 :queries-from "typescript"
 :queries {:indents ["tsx/indents.janet"]
  :tags ["javascript/upstream/tags.janet"
         "typescript/upstream/tags.janet"
         "typescript/tags.janet"]}
 :lsp-root-markers ["package.json" "tsconfig.json"]
 :import-resolution {:extensions ["ts" "tsx" "js" "jsx" "mjs" "cjs"] :index-basenames ["index"] :search-package-dirs true}
 :injection-aliases ["jsx"]
 :snippets
 {
   "rfc"
   "function ${1:Component}() {\n    return (\n        <div>$0</div>\n    );\n}"
   "usestate"
   "const [${1:state}, set${2:State}] = useState(${3:initialValue});$0"
   "useeffect"
   "useEffect(() => {\n    $0\n}, [${1:deps}]);"
   "if"
   "if (${1:condition}) {\n    $0\n}"
   "log"
   "console.log(${1:value});$0"
   "import"
   "import ${1:module} from '${2:package}';$0"}
}
