# configurable-formatter-rules follow-up: the eighth language over the
# shared capture template, over tree-sitter-c-sharp's own node types --
# every shape verified live against the real grammar before this file was
# written (a throwaway sexp-dump probe first, the same method
# rust/rollout-follow-up #14 found catches more up front than reading
# node-types.json alone).
(method_declaration body: (block) @brace.function)

# brace.control: every construct whose body/consequence field is a plain
# (block); switch's own body is "switch_body", the same distinct name
# cpp/java's own switch carries.
(if_statement consequence: (block) @brace.control)
(while_statement body: (block) @brace.control)
(for_statement body: (block) @brace.control)
(foreach_statement body: (block) @brace.control)
(switch_statement body: (switch_body) @brace.control)
(catch_clause body: (block) @brace.control)

# brace.class: class/struct/record bodies all share "declaration_list",
# the same node type already load-bearing for def.method below --
# confirmed live struct/record follow the identical shape as class, not
# assumed from class's own. A positional record ("record R(int X);") has
# no `body` field at all when it ends in ";" -- verified live, so it
# never matches, the same "no brace, no capture" precedent every prior
# language's bodyless shapes already set.
(class_declaration body: (declaration_list) @brace.class)
(struct_declaration body: (declaration_list) @brace.class)
(record_declaration body: (declaration_list) @brace.class)

# brace.interface: kept distinct, same precedent go/php/rust's own
# brace.interface set.
(interface_declaration body: (declaration_list) @brace.interface)

# brace.namespace: same name/precedent cpp's own namespace_definition
# set. C#10's file-scoped "namespace N;" form has no body/braces at all
# (a distinct node type, file_scoped_namespace_declaration) so it's simply
# never named here -- nothing to capture.
(namespace_declaration body: (declaration_list) @brace.namespace)

# control.parens: unlike cpp/java/javascript (whose condition field IS a
# node spanning the whole "(...)"), C#'s if/while/switch condition/value
# field is a plain `expression`/bare node with the "(" ")" as unwrapped
# anonymous tokens -- confirmed against node-types.json (condition:
# {"types": [{"type": "expression"}]}, no parenthesized wrapper) and live
# (an "if (x)" condition node is just the identifier "x", NOT spanning the
# parens at all). So even though these parens are MANDATORY, not an
# optional python/go-style lever, they need the SAME paired mechanism a
# for-loop's own clause does in every language -- a bare `condition:`
# capture here would hand :within a span with no parens in it at all.
# Deliberately no separate lever for a genuinely redundant double-paren
# ("if ((x))"): condition: (parenthesized_expression) IS a real, matchable
# shape, but it would overlap the outer paired capture's own span rather
# than sit beside it the way python/go's narrow lever safely does (their
# languages have no OUTER paired capture to overlap with) -- declined
# rather than risking two "control.parens" captures double-editing the
# same gap, the same class of hazard :within on an empty pair already
# taught.
(if_statement "(" @control.parens.open ")" @control.parens.close)
(while_statement "(" @control.parens.open ")" @control.parens.close)
(switch_statement "(" @control.parens.open ")" @control.parens.close)

# paired-delimiter-captures follow-up: a for-loop's own
# "(init; condition; update)" has no single spanning node here either
# (three independent fields around bare anonymous tokens, the same shape
# every other language's for-loop has), needing the paired mechanism.
# A catch clause's own "(Type name)" is DIFFERENT: catch_declaration is a
# real named node, and confirmed via grammar.json's own rule (not
# node-types.json's field list, which only names FIELDS and doesn't say
# what a node's own byte span covers) that its rule literally starts with
# the "(" token and ends with the ")" token -- so the node's own span
# already covers the whole parenthesized clause, captured directly like
# cpp's own named `parameter_list`, no pairing needed. (First guess here
# was wrong -- assumed "no field means no parens in the span," verified
# live before shipping and fixed.)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(foreach_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause (catch_declaration) @control.parens)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per statement-block construct above (brace.class/brace.interface/
# brace.namespace excluded, the same scope every prior language draws).
# csharp's own "block" wraps its statements directly (verified live, no
# Go-style intermediate wrapper), so the "." anchor sits right against
# `block` itself, same as cpp/javascript/java/rust rather than go's
# one-level-deeper anchor. switch_body's own children are switch_section
# nodes directly, anchors the same way.
(method_declaration body: (block . (_) .) @brace.function.simple)
(if_statement consequence: (block . (_) .) @brace.control.simple)
(while_statement body: (block . (_) .) @brace.control.simple)
(for_statement body: (block . (_) .) @brace.control.simple)
(foreach_statement body: (block . (_) .) @brace.control.simple)
(switch_statement body: (switch_body . (_) .) @brace.control.simple)
(catch_clause body: (block . (_) .) @brace.control.simple)

# blank-lines-kind follow-up: def.toplevel/def.method, the same names
# every prior language's file carries -- rolled out from day one this
# time, per the rust-rollout lesson that a new language should get the
# full template rather than Blank bolted on later.
(compilation_unit [(class_declaration) (struct_declaration) (interface_declaration) (enum_declaration)
                    (record_declaration) (namespace_declaration)] @def.toplevel)
# def.method: class/struct/record/namespace bodies all share the SAME
# "declaration_list" node type (confirmed live, already load-bearing for
# brace.class/brace.namespace above), so one pattern covers a method
# nested in any of them -- a namespace-nested top-level-STYLE method is
# rare in idiomatic C# (methods live in a class, never bare in a
# namespace), so this is inert there in practice rather than a real
# ambiguity the way rust's own mod-nested-function case is.
(declaration_list (method_declaration) @def.method)

(compilation_unit . [(class_declaration) (struct_declaration) (interface_declaration) (enum_declaration)
                      (record_declaration) (namespace_declaration)] @def.toplevel.first)
(declaration_list . (method_declaration) @def.method.first)
