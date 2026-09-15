# configurable-formatter-rules follow-up: TypeScript is embedded as a pure
# DELTA over javascript/format.janet (see typescript/language.janet's own
# comment on why), so this file names only constructs JS's grammar has no
# equivalent for at all -- interface, enum, abstract class, type alias.
# Every shared construct (function/class/if/while/for/switch/catch,
# control.parens, def.toplevel/def.method's own function/class/method
# cases) is inherited verbatim from javascript/format.janet, confirmed
# live it matches unmodified against a real typescript parse (the shared
# node types are byte-identical between the two grammars).

# brace.interface: distinct name, same precedent go/php/rust/csharp's own
# brace.interface set. interface_body holds ABSTRACT signatures only
# (method_signature/property_signature, no body field ever), so it never
# needs a def.method contribution the way a real class body does for
# executable methods -- but is still captured below FOR blank-line
# purposes, since JetBrains-style spacing rules apply to interface members
# the same as class ones.
(interface_declaration body: (interface_body) @brace.interface)

# brace.class: abstract_class_declaration is a DISTINCT node type from
# class_declaration (not merely a modifier on it -- confirmed live), so
# javascript/format.janet's own `(class_declaration body: (class_body))`
# pattern does not match it at all. Its own class_body still holds real
# method_definition nodes for any concrete (non-abstract) methods, so
# javascript's own `def.method`/`brace.function`-adjacent method captures
# (anchored on class_body/method_definition themselves, not on which
# node contains the class_body) already cover those with no extra work
# here -- only the class-level brace capture itself needs restating.
(abstract_class_declaration body: (class_body) @brace.class)

# blank-lines-kind follow-up: def.toplevel widened for TS-only top-level
# shapes. javascript/format.janet's own def.toplevel already covers a
# BARE function/class and an export_statement wrapping ANY declaration
# (interface/enum/abstract-class/type-alias included, since it captures
# the wrapper regardless of what's inside) -- these four patterns are only
# needed for the UNEXPORTED, bare top-level form.
(program [(interface_declaration) (enum_declaration) (abstract_class_declaration) (type_alias_declaration)]
  @def.toplevel)
(program . [(interface_declaration) (enum_declaration) (abstract_class_declaration) (type_alias_declaration)]
  @def.toplevel.first)

# def.method: an interface's own abstract method signature -- no body,
# never eligible for brace.function, but still a member JetBrains-style
# spacing rules apply to (the same reasoning java/format.janet's own
# interface_body (method_declaration) capture already documents). An
# abstract class's own abstract member is a DIFFERENT node type again
# (abstract_method_signature, not method_signature -- verified live,
# tree-sitter-typescript gives every one of these three "no body" method
# shapes its own distinct node type) sitting in the SAME class_body a
# concrete method_definition does, so it needs its own pattern beside the
# one inherited from javascript/format.janet.
(interface_body (method_signature) @def.method)
(class_body (abstract_method_signature) @def.method)

(interface_body . (method_signature) @def.method.first)
(class_body . (abstract_method_signature) @def.method.first)

# Deliberately declined: TypeScript's legacy `namespace N { ... }`/
# `module N { ... }` syntax (internal_module) -- verified live it parses
# wrapped in an `expression_statement` with no field name of its own,
# an unusual enough shape (and a legacy feature ES modules have mostly
# superseded) that it's left uncaptured rather than guessed at.
