# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
#
# Token precedence is deliberately absent: with every value token at the same
# precedence the longest match wins, which is what keeps "on-failure" one text
# value while a bare "off" still reads as a boolean (rule order breaks the
# tie). systemd only honours a comment on a line of its own, so "#" and ";"
# are ordinary characters inside a value.
{:name "systemd"
 :extras [(:pattern "[ \\t]")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {document (:repeat (:choice comment section _newline))
  comment (:seq (:choice "#" ";") (:pattern "[^\\r\\n]*") _newline)
  section (:prec-right 0 (:seq section_header (:repeat (:choice directive comment _newline))))
  section_header (:seq "[" section_name "]" _newline)
  section_name (:pattern "[A-Za-z][A-Za-z0-9_.-]*")
  directive (:seq directive_name "=" (:choice directive_value :blank) _newline)
  directive_name (:pattern "[A-Za-z][A-Za-z0-9_-]*")
  directive_value (:repeat1
                   (:choice
                    line_continuation
                    boolean_value
                    specifier
                    env_variable
                    quoted_string
                    escape_sequence
                    number_value
                    text_value
                    (:alias (:token "%") text_value)))
  boolean_value (:token
                 (:choice
                  "yes"
                  "no"
                  "true"
                  "false"
                  "on"
                  "off"
                  "Yes"
                  "No"
                  "True"
                  "False"
                  "On"
                  "Off"))
  number_value (:token
                (:seq
                 (:pattern "[0-9]+")
                 (:choice
                  (:choice
                   "s"
                   "ms"
                   "us"
                   "min"
                   "h"
                   "d"
                   "w"
                   "M"
                   "K"
                   "G"
                   "T"
                   "P"
                   "E"
                   "KiB"
                   "MiB"
                   "GiB"
                   "TiB"
                   "PiB"
                   "EiB"
                   "KB"
                   "MB"
                   "GB"
                   "TB"
                   "PB"
                   "EB")
                  :blank)))
  specifier (:token (:seq "%" (:pattern "[a-zA-Z%]")))
  env_variable (:token
                (:choice
                 (:seq "$" (:pattern "[A-Za-z_][A-Za-z0-9_]*"))
                 (:seq "${" (:pattern "[A-Za-z_][A-Za-z0-9_]*") "}")))
  quoted_string (:choice
                 (:seq "\"" (:choice (:token-immediate (:pattern "([^\"\\\\\\r\\n]|\\\\[^\\r\\n])+")) :blank) "\"")
                 (:seq "'" (:choice (:token-immediate (:pattern "([^'\\\\\\r\\n]|\\\\[^\\r\\n])+")) :blank) "'"))
  # A backslash continues the line only at its end; anywhere else it escapes
  # the character after it, so both are whole tokens and the longer one wins.
  line_continuation (:token (:seq "\\" (:pattern "\\r?\\n")))
  escape_sequence (:token (:seq "\\" (:pattern "[^\\r\\n]")))
  text_value (:token (:pattern "[^\\s$%\"'\\\\][^\\r\\n$%\"'\\\\]*"))
  _newline (:pattern "\\r?\\n")}}
