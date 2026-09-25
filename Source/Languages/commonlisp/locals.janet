#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Lambda-list keywords (&optional, &rest, &key, ...) name nothing; a
#; parameter with a default is the first symbol of its list.

(defun) @local.scope

((list_lit . (sym_lit) @_form) @local.scope
 (:any-of? @_form "let" "let*" "lambda" "flet" "labels" "dolist" "dotimes" "destructuring-bind"
  "multiple-value-bind" "do" "do*"))

((defun_header lambda_list: (list_lit (sym_lit) @local.definition.parameter))
 (:not-match? @local.definition.parameter "^&"))
(defun_header lambda_list: (list_lit (list_lit . (sym_lit) @local.definition.parameter)))

((list_lit . (sym_lit) @_form . (list_lit (sym_lit) @local.definition.parameter))
 (:eq? @_form "lambda")
 (:not-match? @local.definition.parameter "^&"))

((list_lit . (sym_lit) @_form . (list_lit (list_lit . (sym_lit) @local.definition.var)))
 (:any-of? @_form "let" "let*" "do" "do*"))
((list_lit . (sym_lit) @_form . (list_lit (sym_lit) @local.definition.var))
 (:any-of? @_form "let" "let*"))
((list_lit . (sym_lit) @_form . (list_lit . (sym_lit) @local.definition.var))
 (:any-of? @_form "dolist" "dotimes"))
((list_lit . (sym_lit) @_form . (list_lit (sym_lit) @local.definition.var))
 (:any-of? @_form "multiple-value-bind" "destructuring-bind"))

#; A plain let binds its names after its last value.
((list_lit
   . (sym_lit) @_form
   . (list_lit (list_lit . (sym_lit) . (_) @local.initializer) .) @local.declaration)
 (:eq? @_form "let"))
((list_lit
   . (sym_lit) @_form
   . (list_lit (list_lit . (sym_lit) . (_) @local.initializer) @local.declaration))
 (:eq? @_form "let*"))

(sym_lit) @local.reference

(defun_header function_name: (sym_lit) @local.skip)
