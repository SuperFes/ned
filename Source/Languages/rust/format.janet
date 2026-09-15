# configurable-formatter-rules follow-up: the sixth brace-carrying
# language over the shared capture template, over tree-sitter-rust's own
# node types -- every shape verified live against the real grammar before
# this file was written, same discipline as every prior language.
(function_item body: (block) @brace.function)

# brace.control: every construct whose "body"/"consequence" field is a
# plain (block) -- if/while/loop/for/match all qualify, match's own body
# node is named "match_block" rather than "block" but spans exactly its
# own braces the same way. Rust's `else` branch (if_expression's own
# "alternative" field) is deliberately NOT captured here, matching cpp/
# javascript/java's own identical scope cut -- an else branch is either
# another block (which recurses through this same pattern via its own
# nested if_expression/consequence, an else-if chain resolving itself
# clause by clause) or a bare block with no field name of its own to
# anchor a *distinct* rule against; verified live that Rust carries none
# of PHP's colon-alternate hazard (there is no second, brace-less syntax
# for any of these constructs) so this needed no special-casing at all.
(if_expression consequence: (block) @brace.control)
(while_expression body: (block) @brace.control)
(loop_expression body: (block) @brace.control)
(for_expression body: (block) @brace.control)
(match_expression body: (match_block) @brace.control)
# A match_arm's own "value:" is only sometimes a block ("_ => { ... }"),
# and even then it isn't captured here -- it's a plain _expression field
# with no distinguishing shape of its own to anchor a pattern against
# (unlike function/if/while/loop/for/match's own body fields, which are
# always exactly one of a small closed set of node types), so widening to
# arm bodies is left as a real, undone scope cut rather than attempted
# and gotten wrong. match's own outer braces are still captured above.

# brace.class: struct/enum/impl bodies -- JetBrains-style "type body"
# grouping, the same one cpp's own class_specifier/struct_specifier share.
# A unit struct ("struct Unit;") and a tuple struct
# ("struct Tup(i32, i32);") both carry no `body` field at all (verified
# live against node-types.json -- ordered_field_declaration_list is a
# SEPARATE, non-brace-carrying alternative for the tuple-struct form), so
# neither ever matches this pattern -- there is no brace to place.
(struct_item body: (field_declaration_list) @brace.class)
(enum_item body: (enum_variant_list) @brace.class)
(impl_item body: (declaration_list) @brace.class)

# brace.interface: kept distinct from brace.class (a trait's own body is
# an interface-like construct, same precedent go/format.janet's own
# brace.interface set for interface_type).
(trait_item body: (declaration_list) @brace.interface)

# brace.namespace: mod_item's own body -- same name cpp's own
# namespace_definition uses, and the same grammar shape (a bare
# declaration_list, no wrapping node of its own). A bodyless
# "mod foo;" file-per-module declaration has no `body` field at all
# (verified live), so it never matches -- there is no brace there either.
(mod_item body: (declaration_list) @brace.namespace)

# control.parens: if/while/match's own condition/value is a plain
# expression field, not a required-parenthesized one -- idiomatic Rust
# omits parens entirely ("if x {", "match x {"), but the grammar still
# allows writing them (parenthesized_expression is one of `_expression`'s
# own subtypes per node-types.json), so this is the same narrow lever
# go/python's own control.parens already is: it only matches when parens
# are actually present, verified live.
#
# for_expression's own "value" (the iterable) is deliberately NOT
# captured here, unlike match's "value" -- and unlike go's own for-loop
# omission, this is a scope choice, not a grammar limitation: verified
# live that "for x in (0..3) {" parses fine, value: (parenthesized_expression)
# included, so the lever exists. It's declined because a for-loop's
# iterable isn't a scrutinee/condition the way if/while/match's own value
# is (match and switch test a value against patterns; a for-loop merely
# names a sequence to walk) -- the same category distinction JetBrains'
# own "Parentheses" inspection draws, and not one any prior language in
# this rollout has had to make explicitly since none of their grammars
# offered the redundant lever on a for-loop's iterable at all.
(if_expression condition: (parenthesized_expression) @control.parens)
(while_expression condition: (parenthesized_expression) @control.parens)
(match_expression value: (parenthesized_expression) @control.parens)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per statement-block construct above (brace.class/brace.interface/
# brace.namespace excluded -- collapse-simple is a statement-block
# concept, not a type-body one, the same scope cpp/javascript/java/go/php
# already draw). Rust's own `block` wraps its statements DIRECTLY (no
# Go-style intermediate wrapper -- verified live against
# node-types.json's own "children" shape), so the "." anchor sits right
# against `block` itself, the same as cpp/javascript/java rather than
# go's one-level-deeper anchor. `match_block`'s own children are
# match_arm nodes directly, so it anchors the identical way.
(function_item body: (block . (_) .) @brace.function.simple)
(if_expression consequence: (block . (_) .) @brace.control.simple)
(while_expression body: (block . (_) .) @brace.control.simple)
(loop_expression body: (block . (_) .) @brace.control.simple)
(for_expression body: (block . (_) .) @brace.control.simple)
(match_expression body: (match_block . (_) .) @brace.control.simple)

# blank-lines-kind rollout follow-up: def.toplevel/def.method, the same
# names every prior language's file carries.
(source_file [(function_item) (struct_item) (enum_item) (trait_item) (mod_item) (impl_item)]
  @def.toplevel)
# def.method: impl_item/trait_item/mod_item bodies all share the SAME
# "declaration_list" node type (confirmed live -- the same shape already
# load-bearing for this file's own brace.class/brace.interface/
# brace.namespace captures above), so one pattern covers a function nested
# in any of the three. Deliberately NOT distinguishing "a real impl/trait
# METHOD" from "a free function nested inside a mod block" -- both are
# equally "not top-level" and there is no single node-type-level fact that
# tells them apart (mod_item's own body is the identical declaration_list
# type), the same "close enough to fold together" call this file's own
# brace.class already makes for struct+enum+impl.
(declaration_list (function_item) @def.method)

(source_file . [(function_item) (struct_item) (enum_item) (trait_item) (mod_item) (impl_item)]
  @def.toplevel.first)
(declaration_list . (function_item) @def.method.first)

# anon-function-policy-reversal follow-up (see project memory): closure_
# expression's own "body" field is typed EITHER "_expression" or a bare
# "_" wildcard (node-types.json) -- but `block` is itself one of
# "_expression"'s own real, concrete variants (a block IS a valid Rust
# expression, e.g. `let x = { 1 };`), so a type-qualified "(block)"
# capture already discriminates the real "|x| { ... }" case from a bare-
# expression closure body ("|x| x + 1") with no :match? predicate needed
# -- confirmed this isn't Kotlin's own single-ambiguous-node-type shape
# before reaching for that mechanism.
(closure_expression body: (block) @brace.function)
(closure_expression body: (block . (_) .) @brace.function.simple)

# unsafe_block/async_block's own "block" child is an unconditional,
# unambiguous single child (node-types.json) -- always real braces, no
# discriminator needed.
(unsafe_block (block) @brace.control)
(unsafe_block (block . (_) .) @brace.control.simple)
(async_block (block) @brace.control)
(async_block (block . (_) .) @brace.control.simple)
(const_block body: (block) @brace.control)
(const_block body: (block . (_) .) @brace.control.simple)
