# Shell scripts written as indented strings: stdenv phases and hooks
# (`buildPhase`, `preInstall`, `shellHook`), NixOS service scripts, and the
# script argument of the writeShellScript/runCommand family and
# writeShellApplication's `text`. The offset trims the `''` delimiters.
((binding
   (attrpath (identifier) @_name .)
   (indented_string_expression) @injection.content)
 (:match? @_name "^(.+Phase|(pre|post)[A-Z].*|shellHook|script|preStart|postStart|preStop|postStop)$")
 (:offset! @injection.content 0 2 0 -2)
 (:set! injection.language "bash"))

((apply_expression
   (_) @_fn
   (indented_string_expression) @injection.content)
 (:match? @_fn "\\b(writeShellScript(Bin)?|writeBash(Bin)?|runCommand(Local|CC|NoCC)?)\\b")
 (:offset! @injection.content 0 2 0 -2)
 (:set! injection.language "bash"))

((apply_expression
   (_) @_fn
   (attrset_expression
     (binding_set
       (binding
         (attrpath (identifier) @_name .)
         (indented_string_expression) @injection.content))))
 (:match? @_fn "\\bwriteShellApplication$")
 (:eq? @_name "text")
 (:offset! @injection.content 0 2 0 -2)
 (:set! injection.language "bash"))
