# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "editorconfig"
 :extras [(:pattern "[ \\t]")]
 :conflicts []
 :precedences []
 :externals [_end_of_file _integer_range_start]
 :inline []
 :supertypes []
 :rules
 {editorconfig (:seq (:choice preamble :blank) (:repeat section))
  comment (:seq (:pattern "[#;].*") _eol)
  _line (:choice pair comment (:pattern "\\r?\\n"))
  preamble (:repeat1 _line)
  section (:seq header (:repeat _line))
  header (:seq "[" glob "]" _eol)
  glob (:prec-right 0
        (:repeat1
         (:choice
          "/"
          wildcard
          integer_range
          brace_expansion
          character_choice
          character_escape
          (:pattern "[^?*{}\\[\\]/\\r\\n]"))))
  wildcard (:token (:choice "*" "**" "?"))
  character_escape (:pattern "\\\\[?*{},\\[\\]\\\\]")
  brace_expansion (:seq "{" (:repeat (:choice "," glob)) "}")
  integer_range (:seq
                 "{"
                 (:field :start (:alias _integer_range_start integer))
                 (:token-immediate "..")
                 (:field :end (:alias (:token-immediate (:pattern "-?\\d+")) integer))
                 "}")
  character (:pattern "[^\\]\\r\\n]")
  character_range (:seq
                   (:field :start character)
                   (:token-immediate "-")
                   (:field :end (:alias (:token-immediate (:pattern "[^\\]\\r\\n]")) character)))
  character_choice (:seq
                    "["
                    (:choice (:token-immediate "!") :blank)
                    (:repeat1
                     (:choice
                      character
                      character_range
                      (:alias (:pattern "\\\\[-\\]\\\\]") character_escape)))
                    "]")
  pair (:seq
        (:field :key property)
        "="
        (:token (:repeat (:pattern "[ \\t]")))
        (:choice (:field :value string) :blank)
        _eol)
  property (:pattern "[^\\s=#;\\[]([^\\n\\r=]*[^\\s=])?")
  string (:pattern "\\S([^\\n\\r]*\\S)?")
  _eol (:choice (:pattern "\\r?\\n") _end_of_file)}}
