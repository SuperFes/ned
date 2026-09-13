{:name "cmake"
 :extensions [".cmake"]
 :filenames ["CMakeLists.txt"]
 :line-comment "#"
 :snippets
 {
   "fn"
   "function(${1:name})\n    $0\nendfunction()"
   "macro"
   "macro(${1:name})\n    $0\nendmacro()"
   "if"
   "if(${1:condition})\n    $0\nendif()"
   "foreach"
   "foreach(${1:item} IN LISTS ${2:list})\n    $0\nendforeach()"}
}
