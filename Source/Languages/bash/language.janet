{:name "bash"
 :extensions [".sh" ".bash"]
 :line-comment "#"
 :import-resolution {:extensions ["sh"]}
 :injection-aliases ["sh" "shell" "zsh"]
 :snippets
 {
   "shebang"
   "#!/usr/bin/env bash\nset -euo pipefail\n\n$0"
   "for"
   "for ${1:item} in ${2:list}; do\n    $0\ndone"
   "while"
   "while ${1:condition}; do\n    $0\ndone"
   "if"
   "if ${1:[ condition ]}; then\n    $0\nfi"
   "ifelse"
   "if ${1:[ condition ]}; then\n    $2\nelse\n    $0\nfi"
   "fn"
   "${1:name}() {\n    $0\n}"}
}
