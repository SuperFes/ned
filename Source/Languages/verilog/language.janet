# .v is Verilog's unless it reads as V (see v/language.janet); .vh/.sv/.svh are its own.
{:name "verilog"
 :extensions [".v" ".vh" ".sv" ".svh"]
 :injection-aliases ["sv" "systemverilog"]
 :line-comment "//"
 :signature-template "module __ned_m; function void __ned_sig({}); endfunction endmodule"
 :import-resolution {:extensions ["vh" "svh" "v" "sv"]}
 :not-applicable {:injections "nothing in it is written in another language"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :style      "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
