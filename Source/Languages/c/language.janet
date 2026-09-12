{:name "c"
 :extensions [".c" ".h"]
 :line-comment "//"

 # The richer C-family line-inspect node set -- see Languages/CLike.cpp.
 :escapes ["c.line-inspect"]
 :lsp-root-markers ["compile_commands.json" ".clangd" "CMakeLists.txt"]
 :injection-aliases ["h"]
 :snippets
 {
   "main"
   "int main(int argc, char *argv[]) {\n    $0\n    return 0;\n}"
   "for"
   "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n    $0\n}"
   "while"
   "while (${1:condition}) {\n    $0\n}"
   "if"
   "if (${1:condition}) {\n    $0\n}"
   "ifelse"
   "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"
   "fn"
   "${1:void} ${2:name}($3) {\n    $0\n}"
   "struct"
   "struct ${1:Name} {\n    $0\n};"
   "inc"
   "#include <${1:stdio.h}>"
   "printf"
   "printf(\"${1:%s\\n}\", ${2:value});$0"
   "guard"
   "#ifndef ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n#define ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n\n$0\n\n#endif"}
}
