# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "requirements"
 :word package
 :extras [linebreak]
 :conflicts [[requirement]]
 :precedences []
 :externals []
 :inline [_package_list _end_of_line]
 :supertypes []
 :rules
 {file (:repeat
        (:choice
         (:seq (:choice _space :blank) (:choice comment :blank) (:pattern "\\r?\\n"))
         (:seq
          (:choice _space :blank)
          (:choice requirement url (:alias (:pattern "[./]\\S*") path) global_opt)
          _end_of_line)))
  requirement (:seq
               package
               (:choice extras :blank)
               (:choice (:choice version_spec url_spec) :blank)
               (:choice marker_spec :blank)
               (:repeat requirement_opt))
  package (:pattern "[a-zA-Z0-9]([a-zA-Z0-9._-]*[a-zA-Z0-9])?")
  extras (:seq
          (:choice _space :blank)
          "["
          (:choice _space :blank)
          _package_list
          (:choice _space :blank)
          "]")
  _package_list (:seq
                 (:choice _space :blank)
                 package
                 (:repeat (:seq (:choice _space :blank) "," (:choice _space :blank) package)))
  url_spec (:seq (:choice _space :blank) "@" (:choice _space :blank) url)
  url (:seq
       (:field :scheme
        (:choice (:pattern "[a-zA-Z+]+:\\/\\/") (:pattern "[bB][zZ][rR]\\+[lL][pP]:")))
       (:repeat1 (:choice env_var (:pattern "\\S"))))
  version_spec (:choice
                _version_list
                (:seq (:choice _space :blank) "(" _version_list (:choice _space :blank) ")"))
  _version_list (:prec-left 0
                 (:seq
                  (:choice _space :blank)
                  version_cmp
                  (:choice _space :blank)
                  version
                  (:repeat
                   (:seq
                    (:choice _space :blank)
                    ","
                    (:choice _space :blank)
                    version_cmp
                    (:choice _space :blank)
                    version))))
  version (:pattern "[a-zA-Z0-9*!+._-]+")
  version_cmp (:token (:choice "<" "<=" "!=" "==" ">=" ">" "!=" "===" "~="))
  marker_spec (:prec-right 0 (:seq (:choice _space :blank) ";" (:choice _space :blank) _marker))
  marker_op (:choice "in" (:seq "not" _space "in"))
  marker_var (:choice
              "python_version"
              "python_full_version"
              "os_name"
              "sys_platform"
              "platform_release"
              "platform_system"
              "platform_version"
              "platform_machine"
              "platform_python_implementation"
              "implementation_name"
              "implementation_version"
              "extra")
  _marker (:choice _marker_expr _marker_or _marker_and _marker_paren)
  _marker_expr (:seq
                marker_var
                (:choice _space :blank)
                (:choice version_cmp marker_op)
                (:choice _space :blank)
                quoted_string)
  _marker_paren (:prec-left 0
                 (:seq "(" (:choice _space :blank) _marker (:choice _space :blank) ")"))
  _marker_and (:prec-left 0
               (:seq
                _marker
                (:choice _space :blank)
                (:alias "and" marker_op)
                (:choice _space :blank)
                _marker))
  _marker_or (:prec-left 0
              (:seq
               _marker
               (:choice _space :blank)
               (:alias "or" marker_op)
               (:choice _space :blank)
               _marker))
  global_opt (:prec-left 1
              (:choice
               (:seq
                (:alias
                 (:choice "--no-binary" "--only-binary" "--trusted-host" "--use-feature")
                 option)
                (:choice "=" _space)
                (:choice argument quoted_string))
               (:alias (:choice "--no-index" "--prefer-binary" "--require-hashes" "--pre") option)
               (:seq (:alias "-i" option) (:choice _space :blank) (:choice url quoted_string))
               (:seq
                (:alias (:choice "--index-url" "--extra-index-url") option)
                (:choice "=" _space)
                (:choice url quoted_string))
               (:seq
                (:alias (:choice "-c" "-r") option)
                (:choice _space :blank)
                (:choice (:alias argument path) quoted_string))
               (:seq
                (:alias (:choice "--constraint" "--requirement") option)
                (:choice "=" _space)
                (:choice (:alias argument path) quoted_string))
               (:seq
                (:alias (:choice "-e" "-f") option)
                (:choice _space :blank)
                (:choice (:alias argument path) quoted_string url))
               (:seq
                (:alias (:choice "--editable" "--find-links") option)
                (:choice "=" _space)
                (:choice (:alias argument path) quoted_string url))))
  requirement_opt (:seq
                   (:choice _space :blank)
                   (:alias (:choice "--global-option" "--config-settings" "--hash") option)
                   (:choice "=" _space)
                   (:choice argument quoted_string))
  argument (:repeat1 (:pattern "(\\S|\\\\ )"))
  quoted_string (:choice
                 (:seq "\"" (:field :content (:pattern "([^\"]|\\\\\")+")) "\"")
                 (:seq "'" (:field :content (:pattern "([^']|\\\\')+")) "'"))
  env_var (:seq "${" (:field :name (:pattern "[A-Z0-9_]+")) "}")
  linebreak (:seq "\\" _end_of_line)
  _end_of_line (:choice
                (:seq _space comment (:pattern "\\r?\\n"))
                (:seq (:choice _space :blank) (:pattern "\\r?\\n")))
  comment (:pattern "#[^\\n]*")
  _space (:prec -1 (:repeat1 (:pattern "[ \\t]")))}}
