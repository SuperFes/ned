; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture. Every TypeScript-only container (interface bodies, object types,
; enum bodies) is a bracket body the imprint reports -- including
; interface_body, an alias() of object_type that inference could not see
; until it learned to read alias(), and which this file was once wrongly
; blamed for naming. Checked against tree-sitter-typescript's own
; node-types.json.
;
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
