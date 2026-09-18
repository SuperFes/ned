# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "git_config"
 :extras [(:pattern "[ \\t\\f\\v]")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {config (:repeat (:choice section (:seq (:choice comment :blank) (:pattern "\\r?\\n"))))
  section (:seq section_header _section_body)
  section_header (:seq "[" section_name (:choice (:seq "\"" subsection_name "\"") :blank) "]")
  section_name (:pattern "[\\w\\.]+")
  _section_body (:prec-right 0
                 (:seq
                  (:pattern "\\r?\\n")
                  (:repeat
                   (:seq
                    (:seq (:choice variable :blank) (:choice comment :blank))
                    (:pattern "\\r?\\n")))))
  subsection_name (:repeat1 (:choice (:pattern "[^\\r\\n\\x00\\\"\\\\]+") escape_sequence))
  variable (:seq (:choice (:seq name "=" (:field :value _value)) name))
  name (:pattern "[a-zA-Z][\\w\\-]*")
  _value (:choice _boolean integer string)
  _boolean (:choice
            (:alias (:choice (:ref "true") "yes" "on") (:ref "true"))
            (:alias (:choice (:ref "false") "no" "off") (:ref "false")))
  (:ref "true") "true"
  (:ref "false") "false"
  integer (:pattern "\\d+[kmgtpezyKMGTPEZY]?")
  string (:choice _shell_command_string (:repeat1 _string_fragment))
  _shell_command_string (:seq
                         (:choice
                          (:seq shell_command _quoted_string)
                          (:seq shell_command _unquoted_string)
                          (:seq "\"" shell_command _quoted_string_content "\""))
                         (:repeat _string_fragment))
  _string_fragment (:choice _quoted_string _unquoted_string escape_sequence _line_continuation)
  _quoted_string (:seq "\"" (:choice _quoted_string_content :blank) "\"")
  _quoted_string_content (:repeat1
                          (:choice (:pattern "[^\\\"\\r\\n]") escape_sequence _line_continuation))
  _unquoted_string (:choice (:pattern "[^\\r\\n;#\" \\t\\f\\v\\\\!][^\\r\\n;#\"\\\\]*") "!")
  shell_command (:prec 2 "!")
  escape_sequence (:pattern "\\\\([btnfr\"\\\\]|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})")
  _line_continuation (:seq "\\" (:pattern "\\r?\\n"))
  comment (:seq (:pattern "[#;]") (:choice (:pattern "[^\\r\\n]+") :blank))}}
