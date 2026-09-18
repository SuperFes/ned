#; Highlights, ned-authored: tree-sitter/tree-sitter-verilog ships no
#; queries. Its comments are extras with no node, so they cannot be
#; coloured from here; the rest is keyword tokens and a few literals.
(double_quoted_string) @string
(primary_literal) @number
(simple_identifier) @variable
(module_header
  (simple_identifier) @type)
(function_identifier
  (simple_identifier) @function)
(task_identifier
  (simple_identifier) @function)
(text_macro_name) @constant.macro
[
  "module" "endmodule" "input" "output" "inout" "wire" "reg" "logic" "always"
  "always_comb" "always_ff" "always_latch" "begin" "end" "if" "else" "for" "while"
  "case" "endcase" "default" "function" "endfunction" "task" "endtask" "class"
  "endclass" "package" "endpackage" "interface" "endinterface" "assign" "initial"
  "parameter" "localparam" "integer" "int" "bit" "byte" "posedge" "negedge"
  "generate" "endgenerate" "typedef" "struct" "enum" "import" "return" "extends"
  "virtual" "extern" "automatic" "const" "signed" "unsigned" "genvar" "fork" "join"
] @keyword
