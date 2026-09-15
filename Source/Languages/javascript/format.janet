# configurable-formatter-rules follow-up: cpp/format.janet's own two pilot
# captures, rolled out to a second language deliberately -- proving the
# "<language>/<capture>" override tier (Editor/FormatRules.h) gets exercised
# by a real grammar difference, not just a hand-built test. Same capture
# NAMES as cpp's (a rule written once applies to both); different grammar
# node types underneath, per tree-sitter-javascript's own node-types.json --
# a function declaration's body is "statement_block" (cpp: "compound_statement"),
# and an if/while's own condition is "parenthesized_expression" (cpp:
# "condition_clause"). Both still span exactly their own brace/parens pair,
# which is the only contract Editor/FormatBracePlacement.h and
# Editor/FormatSpacing.h actually depend on.
(function_declaration body: (statement_block) @brace.function)
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

# capture-coverage-widening follow-up: same names as cpp's own widened set
# (see that file's own comment), JavaScript's node types underneath. Each
# field-typed child keeps a braceless body ("if (x) return;") from ever
# matching -- there's no brace there to place.
(if_statement consequence: (statement_block) @brace.control)
(while_statement body: (statement_block) @brace.control)
(for_statement body: (statement_block) @brace.control)
(switch_statement body: (switch_body) @brace.control)
(catch_clause body: (statement_block) @brace.control)
(class_declaration body: (class_body) @brace.class)

# switch's own condition is "value:", not "condition:" -- tree-sitter-
# javascript's own field name, confirmed against node-types.json rather
# than assumed from cpp's. Its span is exactly "(...)" the same way
# if/while's parenthesized_expression is, so the same capture name and
# contract apply with no new C++ code.
(switch_statement value: (parenthesized_expression) @control.parens)

# paired-delimiter-captures follow-up: neither a for-loop's own
# "(init; condition; update)" nor tree-sitter-javascript's own catch_clause
# (a bare "parameter:" field -- identifier/array_pattern/object_pattern,
# no wrapping parens node at all, unlike cpp's own parameter_list) has a
# single node spanning the whole parenthesized clause. Both captured
# directly as a matched pair of single-token captures instead -- see
# cpp/format.janet's own comment on the mechanism. ES2019+'s
# parameter-less `catch { ... }` needs no special-casing here: the pattern
# simply fails to match a catch_clause with no "(" ")" tokens at all,
# verified live (0 matches, not a crash or a false one) before this
# shipped.
(for_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: same names and mechanism as cpp's own widened
# set (see that file's own comment), JavaScript's node types underneath --
# switch's own body is "switch_body", not "statement_block", but the
# "exactly one named child" anchor idiom applies identically (one
# switch_case/switch_default clause).
(function_declaration body: (statement_block . (_) .) @brace.function.simple)
(if_statement consequence: (statement_block . (_) .) @brace.control.simple)
(while_statement body: (statement_block . (_) .) @brace.control.simple)
(for_statement body: (statement_block . (_) .) @brace.control.simple)
(switch_statement body: (switch_body . (_) .) @brace.control.simple)
(catch_clause body: (statement_block . (_) .) @brace.control.simple)

# blank-lines-kind rollout follow-up: same def.toplevel/def.method names
# cpp/python's own files carry. `generator_function_declaration` folds in
# alongside `function_declaration` (same shape, `function*` vs `function`);
# `export_statement` is captured itself, not its own inner `declaration:`
# field -- verified live it wraps with no field name the same way cpp's
# `template_declaration`/Python's `decorated_definition` do, so a blank
# line lands above "export", not between it and what it exports.
(program [(function_declaration) (generator_function_declaration) (class_declaration) (export_statement)]
  @def.toplevel)
# def.method: class_body's own children are field-tagged "member:", unlike
# cpp's bare field_declaration_list children -- confirmed against
# node-types.json/a live parse before writing this, not assumed from cpp's
# shape. One capture covers ordinary/static/generator/async methods alike,
# since tree-sitter-javascript types all of them as plain
# "method_definition" regardless of modifier. The field name is
# deliberately NOT named in the pattern (typescript-rollout follow-up):
# tree-sitter-typescript's own class_body has no "member" field at all
# despite sharing the "class_body"/"method_definition" node type names
# with javascript verbatim (confirmed live -- this file is embedded
# directly into typescript/language.janet's own :format list, and a
# field-tagged pattern failed to even COMPILE against that grammar).
# Dropping the field name changes nothing for javascript itself: every
# method_definition inside a class_body IS the "member" field there, so
# an unqualified "(method_definition)" match is already exactly as
# narrow, just no longer grammar-specific.
(class_body (method_definition) @def.method)

(program . [(function_declaration) (generator_function_declaration) (class_declaration) (export_statement)]
  @def.toplevel.first)
(class_body . (method_definition) @def.method.first)

# coverage-audit follow-up: constructs skipped because they weren't the
# day's focus, not because the grammar lacks them -- each verified
# against node-types.json before landing here.
#
# method_definition's own body is a REQUIRED statement_block
# (node-types.json) -- covers class methods, object-literal methods, and
# constructors alike (one node type for all of them in this grammar).
# This had ZERO placement capture before this pass -- only def.method
# (blank-lines) existed for it, the single highest-impact gap this whole
# audit found across every language.
(method_definition body: (statement_block) @brace.function)
(method_definition body: (statement_block . (_) .) @brace.function.simple)

# do_statement's own body field is typed "statement" (the same abstract
# supertype if/while's own consequence/body fields already narrow to
# statement_block); its own condition is a required parenthesized_
# expression, the same shape if/while's own condition field is.
(do_statement body: (statement_block) @brace.control)
(do_statement body: (statement_block . (_) .) @brace.control.simple)
(do_statement condition: (parenthesized_expression) @control.parens)

# try_statement's OWN body (the try block itself, not catch/finally) and
# finally_clause's own body are both REQUIRED statement_block fields --
# unlike bash/java/c#'s own fieldless finally_clause, javascript's names
# it directly.
(try_statement body: (statement_block) @brace.control)
(try_statement body: (statement_block . (_) .) @brace.control.simple)
(finally_clause body: (statement_block) @brace.control)
(finally_clause body: (statement_block . (_) .) @brace.control.simple)

# class_static_block (ES2022 `static { }`) has a required body field, the
# same shape a class's own body field does.
(class_static_block body: (statement_block) @brace.control)
(class_static_block body: (statement_block . (_) .) @brace.control.simple)

# anon-function-policy-reversal follow-up (see project memory): arrow/
# function-expression bodies now get brace.function everywhere, matching
# declared functions' own placement. arrow_function's own body field is
# typed EITHER "expression" OR "statement_block" (node-types.json) -- two
# structurally DISTINCT node types in the same field slot, so a type-
# qualified capture already discriminates with no :match? predicate
# needed (unlike Kotlin's function_body, where both shapes share one node
# type). function_expression/generator_function's own body is always a
# required statement_block, no ambiguity at all.
(arrow_function body: (statement_block) @brace.function)
(arrow_function body: (statement_block . (_) .) @brace.function.simple)
(function_expression body: (statement_block) @brace.function)
(function_expression body: (statement_block . (_) .) @brace.function.simple)
(generator_function body: (statement_block) @brace.function)
(generator_function body: (statement_block . (_) .) @brace.function.simple)
