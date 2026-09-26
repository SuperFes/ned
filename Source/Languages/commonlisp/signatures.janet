((list_lit
   (defun
     (defun_header
       keyword: (defun_keyword) @_kind
       function_name: (sym_lit) @signature.name
       lambda_list: (list_lit) @signature.parameters))) @signature.definition
 (:not-match? @_kind "lambda"))

(defun_header lambda_list: (list_lit (sym_lit) @parameter.name @parameter))
(defun_header lambda_list: (list_lit (list_lit . (sym_lit) @parameter.name . (_) @parameter.default) @parameter))
((defun_header lambda_list: (list_lit (sym_lit) @parameter.skip))
 (:match? @parameter.skip "^&([oO][pP][tT][iI][oO][nN][aA][lL]|[aA][lL][lL][oO][wW]-.*)$"))
((defun_header lambda_list: (list_lit (sym_lit) @parameter.keyword.marker))
 (:match? @parameter.keyword.marker "^&[kK][eE][yY]$"))
((defun_header lambda_list: (list_lit (sym_lit) @parameter.skip . (sym_lit) @parameter.variadic))
 (:match? @parameter.skip "^&([rR][eE][sS][tT]|[bB][oO][dD][yY])$"))
