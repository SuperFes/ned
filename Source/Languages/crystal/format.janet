# Format captures (see Docs/FormattingRules.md for each name's pass). The
# file and a class body are both `expressions`, so a top-level definition is
# one with no type or module around it.

(((method_def) @def.toplevel)
 (:not-has-ancestor? @def.toplevel class_def module_def struct_def enum_def lib_def method_def))
(([(class_def) (module_def) (struct_def) (enum_def) (lib_def) (annotation_def) (macro_def)] @def.toplevel)
 (:not-has-ancestor? @def.toplevel class_def module_def struct_def enum_def lib_def))
((expressions
   .
   [(method_def) (class_def) (module_def) (struct_def) (enum_def) (lib_def) (annotation_def) (macro_def)] @def.toplevel.first)
 (:not-has-ancestor? @def.toplevel.first class_def module_def struct_def enum_def lib_def))

[(class_def body: (expressions [(method_def) (macro_def)] @def.method))
 (module_def body: (expressions [(method_def) (macro_def)] @def.method))
 (struct_def body: (expressions [(method_def) (macro_def)] @def.method))]
[(class_def body: (expressions . [(method_def) (macro_def)] @def.method.first))
 (module_def body: (expressions . [(method_def) (macro_def)] @def.method.first))
 (struct_def body: (expressions . [(method_def) (macro_def)] @def.method.first))]
