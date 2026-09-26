(function body: (block) @brace.function)
(function body: (block . (_) .) @brace.function.simple)
(anonymous_function body: (block) @brace.function)
(anonymous_function body: (block . (_) .) @brace.function.simple)

(case "{" @brace.control.open "}" @brace.control.close)
(type_definition "{" @brace.class.open "}" @brace.class.close)

(source_file [(function) (type_definition) (type_alias) (constant)] @def.toplevel)
(source_file . [(function) (type_definition) (type_alias) (constant)] @def.toplevel.first)
