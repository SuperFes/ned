# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "make"
 :word word
 :extras [(:pattern "\\s") (:alias (:token (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\") comment]
 :conflicts []
 :precedences []
 :externals []
 :inline [_targets
          _target_pattern
          _prerequisites_pattern
          _prerequisites
          _order_only_prerequisites
          _target_or_pattern_assignment
          _primary
          _name
          _string]
 :supertypes []
 :rules
 {makefile (:repeat _thing)
  _thing (:choice
          rule
          _variable_definition
          _directive
          (:seq _function (:token-immediate (:pattern "[\\r\\n]+"))))
  rule (:choice _ordinary_rule _static_pattern_rule)
  _ordinary_rule (:prec-right 0
                  (:seq
                   _targets
                   (:choice ":" "&:" "::")
                   (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                   (:choice _prerequisites :blank)
                   (:choice recipe (:token-immediate (:pattern "[\\r\\n]+")))))
  _static_pattern_rule (:prec-right 0
                        (:seq
                         _targets
                         ":"
                         (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                         _target_pattern
                         ":"
                         (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                         (:choice _prerequisites_pattern :blank)
                         (:choice recipe (:token-immediate (:pattern "[\\r\\n]+")))))
  _targets (:alias list targets)
  _target_pattern (:field :target (:alias list pattern_list))
  _prerequisites (:choice
                  _normal_prerequisites
                  (:seq (:choice _normal_prerequisites :blank) "|" _order_only_prerequisites))
  _normal_prerequisites (:field :normal (:alias list prerequisites))
  _order_only_prerequisites (:field :order_only (:alias list prerequisites))
  _prerequisites_pattern (:field :prerequisite (:alias list pattern_list))
  recipe (:prec-right 0
          (:choice
           (:seq
            _attached_recipe_line
            (:token-immediate (:pattern "[\\r\\n]+"))
            (:repeat (:choice conditional _prefixed_recipe_line)))
           (:seq
            (:token-immediate (:pattern "[\\r\\n]+"))
            (:repeat1 (:choice conditional _prefixed_recipe_line)))))
  _attached_recipe_line (:seq ";" (:choice recipe_line :blank))
  _prefixed_recipe_line (:seq
                         _recipeprefix
                         (:choice recipe_line :blank)
                         (:token-immediate (:pattern "[\\r\\n]+")))
  recipe_line (:seq
               (:choice
                (:choice (:token (:prec 1 "@")) (:token (:prec 1 "-")) (:token (:prec 1 "+")))
                :blank)
               (:choice
                (:seq
                 (:alias shell_text_with_split shell_text)
                 (:repeat
                  (:seq (:choice _recipeprefix :blank) (:alias shell_text_with_split shell_text)))
                 (:choice _recipeprefix :blank))
                :blank)
               (:alias _shell_text_without_split shell_text))
  _variable_definition (:choice
                        VPATH_assignment
                        RECIPEPREFIX_assignment
                        variable_assignment
                        shell_assignment
                        define_directive)
  VPATH_assignment (:seq
                    (:field :name "VPATH")
                    (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                    (:field :operator (:choice "=" ":=" "::=" "?=" "+="))
                    (:field :value paths)
                    (:token-immediate (:pattern "[\\r\\n]+")))
  RECIPEPREFIX_assignment (:seq
                           (:field :name ".RECIPEPREFIX")
                           (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                           (:field :operator (:choice "=" ":=" "::=" "?=" "+="))
                           (:field :value text)
                           (:token-immediate (:pattern "[\\r\\n]+")))
  variable_assignment (:seq
                       (:choice _target_or_pattern_assignment :blank)
                       _name
                       (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                       (:field :operator (:choice "=" ":=" "::=" "?=" "+="))
                       (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                       (:choice (:field :value text) :blank)
                       (:token-immediate (:pattern "[\\r\\n]+")))
  _target_or_pattern_assignment (:seq
                                 (:field :target_or_pattern list)
                                 ":"
                                 (:choice (:token-immediate (:pattern "[\\t ]+")) :blank))
  shell_assignment (:seq
                    (:field :name word)
                    (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                    (:field :operator "!=")
                    (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                    (:field :value _shell_command)
                    (:token-immediate (:pattern "[\\r\\n]+")))
  define_directive (:seq
                    "define"
                    (:field :name word)
                    (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                    (:choice (:field :operator (:choice "=" ":=" "::=" "?=" "+=")) :blank)
                    (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                    (:token-immediate (:pattern "[\\r\\n]+"))
                    (:choice (:field :value (:alias (:repeat1 _rawline) raw_text)) :blank)
                    (:token (:prec 1 "endef"))
                    (:token-immediate (:pattern "[\\r\\n]+")))
  _directive (:choice
              include_directive
              vpath_directive
              export_directive
              unexport_directive
              override_directive
              undefine_directive
              private_directive
              conditional)
  include_directive (:choice
                     (:seq
                      "include"
                      (:field :filenames list)
                      (:token-immediate (:pattern "[\\r\\n]+")))
                     (:seq
                      "sinclude"
                      (:field :filenames list)
                      (:token-immediate (:pattern "[\\r\\n]+")))
                     (:seq
                      "-include"
                      (:field :filenames list)
                      (:token-immediate (:pattern "[\\r\\n]+"))))
  vpath_directive (:choice
                   (:seq "vpath" (:token-immediate (:pattern "[\\r\\n]+")))
                   (:seq "vpath" (:field :pattern word) (:token-immediate (:pattern "[\\r\\n]+")))
                   (:seq
                    "vpath"
                    (:field :pattern word)
                    (:field :directories paths)
                    (:token-immediate (:pattern "[\\r\\n]+"))))
  export_directive (:choice
                    (:seq "export" (:token-immediate (:pattern "[\\r\\n]+")))
                    (:seq
                     "export"
                     (:field :variables list)
                     (:token-immediate (:pattern "[\\r\\n]+")))
                    (:seq "export" variable_assignment))
  unexport_directive (:choice
                      (:seq "unexport" (:token-immediate (:pattern "[\\r\\n]+")))
                      (:seq
                       "unexport"
                       (:field :variables list)
                       (:token-immediate (:pattern "[\\r\\n]+"))))
  override_directive (:choice
                      (:seq "override" define_directive)
                      (:seq "override" variable_assignment)
                      (:seq "override" undefine_directive))
  undefine_directive (:seq
                      "undefine"
                      (:field :variable word)
                      (:token-immediate (:pattern "[\\r\\n]+")))
  private_directive (:seq "private" variable_assignment)
  conditional (:seq
               (:field :condition _conditional_directives)
               (:choice (:field :consequence _conditional_consequence) :blank)
               (:repeat elsif_directive)
               (:choice else_directive :blank)
               "endif"
               (:token-immediate (:pattern "[\\r\\n]+")))
  elsif_directive (:seq
                   "else"
                   (:field :condition _conditional_directives)
                   (:choice (:field :consequence _conditional_consequence) :blank))
  else_directive (:seq
                  "else"
                  (:token-immediate (:pattern "[\\r\\n]+"))
                  (:choice (:field :consequence _conditional_consequence) :blank))
  _conditional_directives (:choice ifeq_directive ifneq_directive ifdef_directive ifndef_directive)
  _conditional_consequence (:repeat1 (:choice _thing _prefixed_recipe_line))
  ifeq_directive (:seq "ifeq" _conditional_args_cmp (:token-immediate (:pattern "[\\r\\n]+")))
  ifneq_directive (:seq "ifneq" _conditional_args_cmp (:token-immediate (:pattern "[\\r\\n]+")))
  ifdef_directive (:seq
                   "ifdef"
                   (:field :variable _primary)
                   (:token-immediate (:pattern "[\\r\\n]+")))
  ifndef_directive (:seq
                    "ifndef"
                    (:field :variable _primary)
                    (:token-immediate (:pattern "[\\r\\n]+")))
  _conditional_args_cmp (:choice
                         (:seq
                          "("
                          (:choice (:field :arg0 _primary) :blank)
                          ","
                          (:choice (:field :arg1 _primary) :blank)
                          ")")
                         (:seq (:field :arg0 _primary) (:field :arg1 _primary)))
  _variable (:choice variable_reference substitution_reference automatic_variable)
  variable_reference (:seq
                      (:choice "$" "$$")
                      (:choice
                       (:choice
                        (:seq (:token-immediate "(") _primary ")")
                        (:seq (:token-immediate "{") _primary "}"))
                       (:alias (:token-immediate (:pattern ".")) word)))
  substitution_reference (:seq
                          (:choice "$" "$$")
                          (:choice
                           (:seq
                            (:token-immediate "(")
                            (:seq
                             (:field :text _primary)
                             ":"
                             (:field :pattern _primary)
                             "="
                             (:field :replacement _primary))
                            ")")
                           (:seq
                            (:token-immediate "{")
                            (:seq
                             (:field :text _primary)
                             ":"
                             (:field :pattern _primary)
                             "="
                             (:field :replacement _primary))
                            "}")))
  automatic_variable (:seq
                      (:choice "$" "$$")
                      (:choice
                       (:choice
                        (:token-immediate (:prec 1 "@"))
                        (:token-immediate (:prec 1 "%"))
                        (:token-immediate (:prec 1 "<"))
                        (:token-immediate (:prec 1 "?"))
                        (:token-immediate (:prec 1 "^"))
                        (:token-immediate (:prec 1 "+"))
                        (:token-immediate (:prec 1 "/"))
                        (:token-immediate (:prec 1 "*")))
                       (:choice
                        (:seq
                         (:token-immediate "(")
                         (:seq
                          (:choice
                           (:token (:prec 1 "@"))
                           (:token (:prec 1 "%"))
                           (:token (:prec 1 "<"))
                           (:token (:prec 1 "?"))
                           (:token (:prec 1 "^"))
                           (:token (:prec 1 "+"))
                           (:token (:prec 1 "/"))
                           (:token (:prec 1 "*")))
                          (:choice (:choice (:token-immediate "D") (:token-immediate "F")) :blank))
                         ")")
                        (:seq
                         (:token-immediate "{")
                         (:seq
                          (:choice
                           (:token (:prec 1 "@"))
                           (:token (:prec 1 "%"))
                           (:token (:prec 1 "<"))
                           (:token (:prec 1 "?"))
                           (:token (:prec 1 "^"))
                           (:token (:prec 1 "+"))
                           (:token (:prec 1 "/"))
                           (:token (:prec 1 "*")))
                          (:choice (:choice (:token-immediate "D") (:token-immediate "F")) :blank))
                         "}"))))
  _function (:choice function_call shell_function)
  function_call (:seq
                 (:choice "$" "$$")
                 (:token-immediate "(")
                 (:field :function
                  (:choice
                   (:token-immediate "subst")
                   (:token-immediate "patsubst")
                   (:token-immediate "strip")
                   (:token-immediate "findstring")
                   (:token-immediate "filter")
                   (:token-immediate "filter-out")
                   (:token-immediate "sort")
                   (:token-immediate "word")
                   (:token-immediate "words")
                   (:token-immediate "wordlist")
                   (:token-immediate "firstword")
                   (:token-immediate "lastword")
                   (:token-immediate "dir")
                   (:token-immediate "notdir")
                   (:token-immediate "suffix")
                   (:token-immediate "basename")
                   (:token-immediate "addsuffix")
                   (:token-immediate "addprefix")
                   (:token-immediate "join")
                   (:token-immediate "wildcard")
                   (:token-immediate "realpath")
                   (:token-immediate "abspath")
                   (:token-immediate "error")
                   (:token-immediate "warning")
                   (:token-immediate "info")
                   (:token-immediate "origin")
                   (:token-immediate "flavor")
                   (:token-immediate "foreach")
                   (:token-immediate "if")
                   (:token-immediate "or")
                   (:token-immediate "and")
                   (:token-immediate "call")
                   (:token-immediate "eval")
                   (:token-immediate "file")
                   (:token-immediate "value")))
                 (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                 arguments
                 ")")
  arguments (:seq (:field :argument text) (:repeat (:seq "," (:field :argument text))))
  shell_function (:seq
                  (:choice "$" "$$")
                  (:token-immediate "(")
                  (:field :function "shell")
                  (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)
                  _shell_command
                  ")")
  list (:prec 1
        (:seq
         _primary
         (:repeat
          (:seq
           (:choice
            (:token-immediate (:pattern "[\\t ]+"))
            (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
           _primary))
         (:choice (:token-immediate (:pattern "[\\t ]+")) :blank)))
  paths (:seq
         _primary
         (:repeat (:seq (:choice (:token-immediate ":") (:token-immediate ";")) _primary)))
  _primary (:choice word archive _variable _function concatenation string)
  concatenation (:prec-right 0 (:seq _primary (:repeat1 (:prec-left 0 _primary))))
  _name (:field :name word)
  string (:field :string
          (:choice
           (:seq "\"" (:choice _string :blank) "\"")
           (:seq "'" (:choice _string :blank) "'")))
  _string (:repeat1
           (:choice
            _variable
            _function
            (:token (:prec -1 (:pattern "([^'\"$\\r\\n\\\\]|\\\\\\\\|\\\\[^\\r\\n])+")))))
  word (:token
        (:repeat1
         (:choice
          (:pattern "[a-zA-Z0-9%\\+\\-\\.@_\\*\\?\\/]")
          (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
          (:pattern "\\\\[0-9]{3}"))))
  archive (:seq
           (:field :archive word)
           (:token-immediate "(")
           (:field :members list)
           (:token-immediate ")"))
  _recipeprefix "\t"
  _rawline (:token (:pattern ".*[\\r\\n]+"))
  _shell_text_without_split (:choice
                             (:seq
                              (:token
                               (:repeat1
                                (:choice
                                 (:pattern "[^\\$\\r\\n\\\\]")
                                 (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
                                 (:pattern "\\\\[0-9]{3}")
                                 (:pattern "\\\\[^\\n\\r]"))))
                              (:repeat
                               (:seq
                                (:choice
                                 _variable
                                 _function
                                 (:alias "$$" escape)
                                 (:alias "//" escape))
                                (:choice
                                 (:token
                                  (:repeat1
                                   (:choice
                                    (:pattern "[^\\$\\r\\n\\\\]")
                                    (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
                                    (:pattern "\\\\[0-9]{3}")
                                    (:pattern "\\\\[^\\n\\r]"))))
                                 :blank))))
                             (:seq
                              (:choice
                               _variable
                               _function
                               (:alias "$$" escape)
                               (:alias "//" escape))
                              (:repeat
                               (:seq
                                (:choice
                                 (:token
                                  (:repeat1
                                   (:choice
                                    (:pattern "[^\\$\\r\\n\\\\]")
                                    (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
                                    (:pattern "\\\\[0-9]{3}")
                                    (:pattern "\\\\[^\\n\\r]"))))
                                 :blank)
                                (:choice
                                 _variable
                                 _function
                                 (:alias "$$" escape)
                                 (:alias "//" escape))))
                              (:choice
                               (:token
                                (:repeat1
                                 (:choice
                                  (:pattern "[^\\$\\r\\n\\\\]")
                                  (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
                                  (:pattern "\\\\[0-9]{3}")
                                  (:pattern "\\\\[^\\n\\r]"))))
                               :blank)))
  shell_text_with_split (:seq
                         _shell_text_without_split
                         (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
  _shell_command (:alias text shell_command)
  text (:choice
        (:seq
         (:token
          (:repeat1
           (:choice
            (:choice
             (:pattern "[^\\$\\(\\)\\n\\r\\\\]")
             (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
            (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
            (:pattern "\\\\[0-9]{3}")
            (:pattern "\\\\[^\\n\\r]"))))
         (:repeat
          (:seq
           (:choice _variable _function (:alias "$$" escape) (:alias "//" escape))
           (:choice
            (:token
             (:repeat1
              (:choice
               (:choice
                (:pattern "[^\\$\\(\\)\\n\\r\\\\]")
                (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
               (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
               (:pattern "\\\\[0-9]{3}")
               (:pattern "\\\\[^\\n\\r]"))))
            :blank))))
        (:seq
         (:choice _variable _function (:alias "$$" escape) (:alias "//" escape))
         (:repeat
          (:seq
           (:choice
            (:token
             (:repeat1
              (:choice
               (:choice
                (:pattern "[^\\$\\(\\)\\n\\r\\\\]")
                (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
               (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
               (:pattern "\\\\[0-9]{3}")
               (:pattern "\\\\[^\\n\\r]"))))
            :blank)
           (:choice _variable _function (:alias "$$" escape) (:alias "//" escape))))
         (:choice
          (:token
           (:repeat1
            (:choice
             (:choice
              (:pattern "[^\\$\\(\\)\\n\\r\\\\]")
              (:alias (:token-immediate (:seq "\\" (:pattern "\\r?\\n|\\r"))) "\\"))
             (:pattern "\\\\[abtnvfrE!\"#\\$&'\\(\\)\\*,;<>\\?\\[\\\\\\]^`{\\|}~]")
             (:pattern "\\\\[0-9]{3}")
             (:pattern "\\\\[^\\n\\r]"))))
          :blank)))
  comment (:token (:prec -1 (:pattern "#.*")))}}
