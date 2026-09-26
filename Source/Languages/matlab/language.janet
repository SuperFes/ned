# .m belongs to Objective-C unless the file uses forms Objective-C never has:
# a `function`/`classdef` declaration, a `%` comment line, a bare `end`.
# A MATLAB script with none of them still opens as Objective-C.
{:name "matlab"
 :extensions [".mlx"]
 :injection-aliases ["octave"]
 :shared-extensions [".m"]
 :content-pattern "^\\s*(function\\s+[\\w\\[]|classdef\\b|%)|^\\s*(end|endfunction|endif|endfor|endwhile)\\s*;?\\s*$"
 :line-comment "%"
 :signature-template "function ned_sig({})\nend"
 :not-applicable {:imports "files are found by name on the MATLAB path; nothing names a file"
                  :style   "no canonical formatter or official style rule for the braces and blank lines ned's rules cover"}
}
