(clazz (classBody) @brace.class)
(clazz (classBody . (_) .) @brace.class.simple)

(module [(clazz) (classMethod) (typeAlias)] @def.toplevel)
(classBody [(classProperty) (classMethod)] @def.method)
(classBody . [(classProperty) (classMethod)] @def.method.first)
