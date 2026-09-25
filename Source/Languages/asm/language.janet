# GNU as / NASM-style x86 and ARM assembly. .S (preprocessed) is claimed
# too: the grammar treats # lines as comments.
{:name "asm"
 :extensions [".s" ".S" ".asm" ".nasm"]
 :injection-aliases ["nasm" "assembly" "x86asm"]
 :line-comment ";"
 :not-applicable {:indents    "flat instruction lines; nothing nests"
                  :locals     "labels are global symbols; nothing is scoped"
                  :tests      "no test framework runs tests written in it"
                  :signatures "arguments pass in registers; there is no parameter list"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
