(defProc body: (block (kBegin) @brace.function.open (kEnd) @brace.function.close))

(ifElse then: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(ifElse else: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(if then: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(while body: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(for body: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(foreach body: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))
(with body: (block (kBegin) @brace.control.open (kEnd) @brace.control.close))

(ifElse condition: (exprParens) @control.parens)
(if condition: (exprParens) @control.parens)
(while condition: (exprParens) @control.parens)

(ifElse (kElse) @control.keyword)

(program (defProc) @def.toplevel)
(program . (defProc) @def.toplevel.first)
