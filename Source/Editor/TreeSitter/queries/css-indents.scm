; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-css's own node-types.json.
(block) @indent
(keyframe_block_list) @indent

(block "}" @dedent)
(keyframe_block_list "}" @dedent)
