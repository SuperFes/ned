{:name "python"
 :extensions [".py" ".pyw"]
 :line-comment "#"
 :lsp-root-markers ["pyproject.toml" "setup.py" "setup.cfg"]
 :import-resolution {:extensions ["py"] :index-basenames ["__init__"]}
 :injection-aliases ["py"]
 :snippets
 {
   "def"
   "def ${1:name}(${2:args}):\n    $0"
   "class"
   "class ${1:Name}:\n    def __init__(self${2:, args}):\n        $0"
   "for"
   "for ${1:item} in ${2:iterable}:\n    $0"
   "while"
   "while ${1:condition}:\n    $0"
   "if"
   "if ${1:condition}:\n    $0"
   "ifelse"
   "if ${1:condition}:\n    $2\nelse:\n    $0"
   "try"
   "try:\n    $1\nexcept ${2:Exception} as ${3:e}:\n    $0"
   "main"
   "if __name__ == \"__main__\":\n    $0"
   "print"
   "print(${1:value})$0"
   "import"
   "import ${1:module}$0"}
}
