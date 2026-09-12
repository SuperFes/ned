; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. javascript-indents.scm's own set plus TypeScript-only
; container node types (interface bodies, object types, enum bodies) --
; shared by TypeScriptMode and TsxMode, mirroring kTypeScript's own sharing
; (CMakeLists.txt/Mode.cpp). Checked against tree-sitter-typescript's own
; node-types.json.
(statement_block) @indent
(object) @indent
(object_pattern) @indent
(object_type) @indent
(array) @indent
(array_pattern) @indent
(arguments) @indent
(formal_parameters) @indent
(type_parameters) @indent
(class_body) @indent
(interface_body) @indent
(enum_body) @indent
(switch_body) @indent

(statement_block "}" @dedent)
(object "}" @dedent)
(object_pattern "}" @dedent)
(object_type "}" @dedent)
(array "]" @dedent)
(array_pattern "]" @dedent)
(arguments ")" @dedent)
(formal_parameters ")" @dedent)
(type_parameters ">" @dedent)
(class_body "}" @dedent)
(interface_body "}" @dedent)
(enum_body "}" @dedent)
(switch_body "}" @dedent)

; No JSX rules here, deliberately: the `typescript` dialect's compiled parser
; rejects `jsx_element` and friends as unknown node types even though its
; grammar.json still declares them (they are shared source with the tsx
; dialect, which is the one that actually parses them). A query naming a node
; type the parser does not know fails to COMPILE, taking the whole mode's
; indentation down with it -- which is how this was found. tsx-indents.scm is
; typescript's rules plus JSX's, for the dialect that can use them.
;
; Checked against the compiled node-types.json rather than grammar.json: the
; two disagree here, and only one of them is what the parser answers to.
(parenthesized_expression) @aligned
(parenthesized_expression ")" @dedent)
