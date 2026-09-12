{:name "cpp"
 :extensions [".cpp" ".cc" ".cxx" ".hpp" ".hh"]
 :line-comment "//"

 # C's own imports query -- both grammars define preproc_include identically.
 :queries {:imports ["c/imports.janet"]}

 # cpp.test-body widens an unexpanded TEST_CASE macro over its sibling
 # compound_statement -- see Languages/CLike.cpp.
 :escapes ["c.line-inspect" "cpp.test-body"]
 :lsp-root-markers ["compile_commands.json" ".clangd" "CMakeLists.txt"]
 :injection-aliases ["c++" "cc" "cxx" "hpp"]
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
   "class"
   "class ${1:Name} {\n  public:\n    ${1:Name}() = default;\n\n  private:\n    $0\n};"
   "fn"
   "${1:void} ${2:name}($3) {\n    $0\n}"
   "inc"
   "#include <${1:iostream}>"
   "cout"
   "std::cout << ${1:value} << std::endl;$0"
   "try"
   "try {\n    $1\n} catch (const ${2:std::exception}& ${3:e}) {\n    $0\n}"
   "ns"
   "namespace ${1:name} {\n$0\n} // namespace $1"
   "guard"
   "#ifndef ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n#define ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n\n$0\n\n#endif // ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H"}
}
