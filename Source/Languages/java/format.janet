# configurable-formatter-rules follow-up: the third language over cpp/
# javascript's own capture template -- same capture NAMES throughout,
# tree-sitter-java's own node types underneath, every shape verified live
# against the real grammar before this file was written (a multi-
# init/update for-loop's own outer parens in particular, since Java allows
# several comma-separated init/update expressions unlike cpp/javascript's
# single ones -- confirmed the paired "(" ")" capture still grabs the
# OUTER pair regardless).
#
# A method's own body -- method_declaration's "body" field, typed "block".
# Optional in the grammar (an abstract/interface method has none), so this
# simply doesn't match those.
(method_declaration body: (block) @brace.function)

# Every other brace-carrying construct: control-flow bodies share one name
# (brace.control), matching cpp/javascript's own grouping. Java's switch
# is a single "switch_expression" node covering both switch-statement and
# switch-expression usage; its own body is "switch_block", not a "block".
# No "brace.namespace" here -- Java's package declaration isn't
# brace-delimited at all, unlike cpp's namespace.
(if_statement consequence: (block) @brace.control)
(while_statement body: (block) @brace.control)
(for_statement body: (block) @brace.control)
(switch_expression body: (switch_block) @brace.control)
(catch_clause body: (block) @brace.control)
(class_declaration body: (class_body) @brace.class)

# control.parens: if/while/switch's own condition is one node spanning
# exactly "(...)" ("parenthesized_expression"), the same shape cpp's
# condition_clause and javascript's own parenthesized_expression already
# satisfy.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
(switch_expression condition: (parenthesized_expression) @control.parens)

# paired-delimiter-captures follow-up: a for-loop's own "(init; condition;
# update)" has no single spanning node here either (Java's own init/update
# fields are each "multiple: true", allowing several comma-separated
# expressions -- confirmed live that the paired capture still finds the
# OUTER "(" ")" regardless of how many init/update expressions sit between
# them). A catch clause's own parameter is an unnamed
# "catch_formal_parameter" child with no wrapping parens field, the same
# shape javascript's catch has (unlike cpp's own named "parameters" field)
# -- captured the same paired way.
(for_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per brace-carrying construct above (except brace.class -- collapse-simple
# is a statement-block concept, not a type-body one, matching cpp/
# javascript's own scope). Java's switch_block's "exactly one child" means
# exactly one switch_block_statement_group (case group).
(method_declaration body: (block . (_) .) @brace.function.simple)
(if_statement consequence: (block . (_) .) @brace.control.simple)
(while_statement body: (block . (_) .) @brace.control.simple)
(for_statement body: (block . (_) .) @brace.control.simple)
(switch_expression body: (switch_block . (_) .) @brace.control.simple)
(catch_clause body: (block . (_) .) @brace.control.simple)

# blank-lines-kind rollout follow-up: def.toplevel/def.method, the same
# names cpp/javascript/python's own files carry. Java has no top-level
# FUNCTIONS the way cpp/javascript/Python do -- only top-level TYPE
# declarations, so def.toplevel covers just those (class/interface/enum/
# record/annotation-type), matching Python's own class_definition side of
# its alternation with no function_definition equivalent here.
(program [(class_declaration) (interface_declaration) (enum_declaration) (record_declaration)
          (annotation_type_declaration)] @def.toplevel)
# def.method: unlike javascript's own field-tagged "member:" children,
# class_body/interface_body/enum_body_declarations hold method_declaration/
# constructor_declaration as plain untagged children -- confirmed against a
# live parse, not assumed from javascript's shape. Three separate body node
# types (Java has no single shared "declaration_list" the way PHP's
# class/trait/interface bodies do), so three patterns.
(class_body [(method_declaration) (constructor_declaration)] @def.method)
(interface_body (method_declaration) @def.method)
(enum_body_declarations [(method_declaration) (constructor_declaration)] @def.method)

(program . [(class_declaration) (interface_declaration) (enum_declaration) (record_declaration)
            (annotation_type_declaration)] @def.toplevel.first)
(class_body . [(method_declaration) (constructor_declaration)] @def.method.first)
(interface_body . (method_declaration) @def.method.first)
(enum_body_declarations . [(method_declaration) (constructor_declaration)] @def.method.first)

# coverage-audit follow-up: constructs skipped because they weren't the
# day's focus, not because the grammar lacks them -- each verified against
# node-types.json/grammar.json before landing here.
#
# do-while's body field is typed "statement" (the same supertype if/
# while's own consequence/body fields already narrow to "block"), and
# try_statement's OWN body (the try block itself, not catch/finally) is a
# required "block" -- both simply never added.
(do_statement body: (block) @brace.control)
(do_statement body: (block . (_) .) @brace.control.simple)
(try_statement body: (block) @brace.control)
(try_statement body: (block . (_) .) @brace.control.simple)
(try_with_resources_statement body: (block) @brace.control)
(try_with_resources_statement body: (block . (_) .) @brace.control.simple)

# finally_clause/static_initializer have no FIELD at all (node-types.json)
# -- their own "block" is a bare, untagged child, same shape a for-loop's
# anonymous "(" ")" tokens already needed a different mechanism for; here
# a plain child-match suffices since there's exactly one such child.
# A bare instance-initializer block (no "static" keyword) is a real,
# DIFFERENT shape again -- confirmed via class_body's own children list:
# it's a "block" sitting directly among class_body's children, distinct
# from static_initializer's own wrapping node.
(finally_clause (block) @brace.control)
(finally_clause (block . (_) .) @brace.control.simple)
(static_initializer (block) @brace.control)
(static_initializer (block . (_) .) @brace.control.simple)
(class_body (block) @brace.control)
(class_body (block . (_) .) @brace.control.simple)

# switch_rule's own arrow-arm block (Java 14+ "case X -> { }") is one of
# several untagged alternatives among switch_rule's children (the others
# are expression_statement/switch_label/throw_statement, none brace-
# shaped) -- a plain child-match is unambiguous.
(switch_rule (block) @brace.control)
(switch_rule (block . (_) .) @brace.control.simple)

# anon-function-policy-reversal follow-up (see project memory): lambda
# bodies now get brace.function everywhere, matching declared functions'
# own placement. lambda_expression's own "body" field (node-types.json) is
# typed EITHER "block" OR "expression" -- two structurally DISTINCT node
# types in the same field slot, so a type-qualified capture already
# discriminates with no :match? predicate needed (unlike Kotlin's
# function_body, where both shapes share one node type and only their
# own leading byte differs).
(lambda_expression body: (block) @brace.function)
(lambda_expression body: (block . (_) .) @brace.function.simple)

# Anonymous class bodies (`new Foo() { }`) and per-constant enum bodies
# (`RED { ... }`) both wrap a plain class_body -- folded into brace.class,
# the same "close enough" call this file already makes for class/
# interface/enum/record's own top-level bodies.
(object_creation_expression (class_body) @brace.class)
(enum_constant body: (class_body) @brace.class)
