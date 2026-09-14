{:name "cmake"
 :extensions [".cmake"]
 :filenames ["CMakeLists.txt"]
 :line-comment "#"

 # Deliberately no :lsp-root-markers: "CMakeLists.txt" as its own marker
 # would match trivially at a nested CMakeLists.txt's own directory,
 # defeating the nearest-ancestor walk instead of finding the real top --
 # editor::ProjectRoot()'s fallback already lands on the top-level build
 # root for the common case where it IS the project root (this repo
 # included).
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
