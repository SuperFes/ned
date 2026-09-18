# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "udev"
 :extras [(:pattern "[ \\t]") linebreak]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {rules (:repeat
         (:choice (:seq rule (:pattern "\\n")) (:seq comment (:pattern "\\n")) (:pattern "\\n")))
  rule (:seq (:repeat (:seq match ",")) assignment (:repeat (:seq "," (:choice assignment match))))
  match (:choice
         (:seq (:field :key "ACTION") match_op value)
         (:seq (:field :key "DEVPATH") match_op value)
         (:seq (:field :key "KERNEL") match_op value)
         (:seq (:field :key "KERNELS") match_op value)
         (:seq (:field :key "NAME") match_op (:alias _sub_value value))
         (:seq (:field :key "SYMLINK") match_op (:alias _sub_value value))
         (:seq (:field :key "SUBSYSTEM") match_op value)
         (:seq (:field :key "SUBSYSTEMS") match_op value)
         (:seq (:field :key "DRIVER") match_op value)
         (:seq (:field :key "DRIVERS") match_op value)
         (:seq (:field :key "ATTR") (:token-immediate "{") attribute "}" match_op value)
         (:seq (:field :key "ATTRS") (:token-immediate "{") attribute "}" match_op value)
         (:seq (:field :key "SYSCTL") (:token-immediate "{") kernel_param "}" match_op value)
         (:seq
          (:field :key "ENV")
          (:token-immediate "{")
          env_var
          "}"
          match_op
          (:alias _sub_value value))
         (:seq (:field :key "CONST") (:token-immediate "{") system_const "}" match_op value)
         (:seq (:field :key "TAG") match_op value)
         (:seq (:field :key "TAGS") match_op value)
         (:seq
          (:field :key "TEST")
          (:choice (:seq (:token-immediate "{") octal "}") :blank)
          match_op
          value)
         (:seq (:field :key "PROGRAM") (:choice match_op assignment_op) (:alias _sub_value value))
         (:seq (:field :key "RESULT") match_op value))
  assignment (:choice
              (:seq (:field :key "NAME") assignment_op (:alias _sub_value value))
              (:seq (:field :key "SYMLINK") assignment_op (:alias _sub_value value))
              (:seq (:field :key "OWNER") assignment_op (:alias _sub_value value))
              (:seq (:field :key "GROUP") assignment_op (:alias _sub_value value))
              (:seq (:field :key "MODE") assignment_op (:alias _sub_value value))
              (:seq
               (:field :key "SECLABEL")
               (:token-immediate "{")
               seclabel
               "}"
               assignment_op
               (:alias _sub_value value))
              (:seq (:field :key "ATTR") (:token-immediate "{") attribute "}" assignment_op value)
              (:seq
               (:field :key "SYSCTL")
               (:token-immediate "{")
               kernel_param
               "}"
               assignment_op
               value)
              (:seq
               (:field :key "ENV")
               (:token-immediate "{")
               env_var
               "}"
               assignment_op
               (:alias _sub_value value))
              (:seq (:field :key "TAG") assignment_op value)
              (:seq
               (:field :key "RUN")
               (:choice (:seq (:token-immediate "{") run_type "}") :blank)
               assignment_op
               (:alias _sub_value value))
              (:seq (:field :key "LABEL") assignment_op value)
              (:seq (:field :key "GOTO") assignment_op value)
              (:seq
               (:field :key "IMPORT")
               (:token-immediate "{")
               import_type
               "}"
               (:choice assignment_op match_op)
               value)
              (:seq (:field :key "OPTIONS") assignment_op value))
  system_const (:token (:choice "arch" "virt" "cvm"))
  run_type (:token (:choice "program" "builtin"))
  import_type (:token (:choice "program" "builtin" "file" "db" "cmdline" "parent"))
  attribute (:repeat1 (:choice (:pattern "[\\w/.]") pattern fmt_sub))
  env_var (:pattern "[\\w.]+")
  kernel_param (:pattern "[\\w.]+")
  seclabel (:pattern "[a-zA-Z]+")
  octal (:pattern "0?[0-7]{3}")
  number (:pattern "\\d+")
  match_op (:token (:choice "==" "!="))
  assignment_op (:token (:choice "=" "-=" "+=" ":="))
  value (:choice
         (:seq "\"" (:choice content :blank) (:token-immediate "\""))
         (:seq
          "e"
          (:token-immediate "\"")
          (:choice (:alias _c_content content) :blank)
          (:token-immediate "\"")))
  _sub_value (:choice
              (:seq "\"" (:choice (:alias _sub_content content) :blank) (:token-immediate "\""))
              (:seq
               "e"
               (:token-immediate "\"")
               (:choice (:alias _sub_c_content content) :blank)
               (:token-immediate "\"")))
  content (:repeat1 (:choice (:pattern "[^\"]") "\\\"" pattern))
  _sub_content (:repeat1 (:choice (:pattern "[^\"]") "\\\"" pattern fmt_sub var_sub))
  _c_content (:repeat1 (:choice (:pattern "[^\"]") pattern c_escape))
  _sub_c_content (:repeat1 (:choice (:pattern "[^\"]") pattern c_escape fmt_sub var_sub))
  pattern (:choice "*" "?" "|" (:pattern "\\[!?[^\\[\\]\"]+\\]"))
  c_escape (:choice
            (:pattern "\\\\[abefnrtv\\\\'?]")
            (:pattern "\\\\\\d{1,3}")
            (:pattern "\\\\x[0-9A-Fa-f]{2}")
            (:pattern "\\\\u[0-9A-Fa-f]{4}")
            (:pattern "\\\\U[0-9A-Fa-f]{8}"))
  fmt_sub (:choice
           "%k"
           "%n"
           "%p"
           "%b"
           (:seq "%s" (:token-immediate "{") attribute "}")
           (:seq "%E" (:token-immediate "{") env_var "}")
           "%M"
           "%m"
           (:seq
            "%c"
            (:choice
             (:seq (:token-immediate "{") number (:choice "+" :blank) (:token-immediate "}"))
             :blank))
           "%P"
           "%r"
           "%S"
           "%N"
           "%%")
  var_sub (:choice
           "$kernel"
           "$number"
           "$devpath"
           "$id"
           "$driver"
           (:seq "$attr" (:token-immediate "{") attribute "}")
           (:seq "$env" (:token-immediate "{") env_var "}")
           "$major"
           "$minor"
           (:seq
            "$result"
            (:choice (:seq (:token-immediate "{") number (:choice "+" :blank) "}") :blank))
           "$parent"
           "$name"
           "$links"
           "$root"
           "$sys"
           "$devnode"
           "$$")
  linebreak (:token (:prec 1 (:seq "\\" (:pattern "\\n"))))
  comment (:pattern "#.*")}}
