# ned-authored. The imprint indents braced blocks; this adds continuation
# lines. awk lets a line break after `&&`, `||` and `,` and inside
# parentheses, so a wrapped condition or assignment sits a continuation
# step past its first line (see c/indents.janet).
[(binary_exp) (ternary_exp) (assignment_exp)] @indent.continuation
