# .v is Verilog's unless it reads as V (see v/language.janet); .vh/.sv/.svh are its own.
{:name "verilog"
 :extensions [".v" ".vh" ".sv" ".svh"]
 :injection-aliases ["sv" "systemverilog"]
 :line-comment "//"
 :import-resolution {:extensions ["vh" "svh" "v" "sv"]}
 :not-applicable {:injections "nothing in it is written in another language"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
