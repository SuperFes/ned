# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "dockerfile"
 :extras [(:pattern "\\s+") line_continuation comment]
 :conflicts []
 :precedences []
 :externals [heredoc_marker heredoc_line heredoc_end heredoc_nl error_sentinel]
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat (:seq _instruction "\n"))
  _instruction (:choice
                from_instruction
                run_instruction
                cmd_instruction
                label_instruction
                expose_instruction
                env_instruction
                add_instruction
                copy_instruction
                entrypoint_instruction
                volume_instruction
                user_instruction
                workdir_instruction
                arg_instruction
                onbuild_instruction
                stopsignal_instruction
                healthcheck_instruction
                shell_instruction
                maintainer_instruction
                cross_build_instruction)
  from_instruction (:seq
                    (:alias (:pattern "[fF][rR][oO][mM]") "FROM")
                    (:choice param :blank)
                    image_spec
                    (:choice
                     (:seq (:alias (:pattern "[aA][sS]") "AS") (:field :as image_alias))
                     :blank))
  run_instruction (:seq
                   (:alias (:pattern "[rR][uU][nN]") "RUN")
                   (:repeat (:choice param mount_param))
                   (:choice json_string_array shell_command)
                   (:repeat heredoc_block))
  cmd_instruction (:seq
                   (:alias (:pattern "[cC][mM][dD]") "CMD")
                   (:choice json_string_array shell_command))
  label_instruction (:seq (:alias (:pattern "[lL][aA][bB][eE][lL]") "LABEL") (:repeat1 label_pair))
  expose_instruction (:seq
                      (:alias (:pattern "[eE][xX][pP][oO][sS][eE]") "EXPOSE")
                      (:repeat1 (:choice expose_port expansion)))
  env_instruction (:seq
                   (:alias (:pattern "[eE][nN][vV]") "ENV")
                   (:choice (:repeat1 env_pair) (:alias _spaced_env_pair env_pair)))
  add_instruction (:seq
                   (:alias (:pattern "[aA][dD][dD]") "ADD")
                   (:repeat param)
                   (:repeat1 (:seq (:alias path_with_heredoc path) _non_newline_whitespace))
                   (:alias path_with_heredoc path)
                   (:repeat heredoc_block))
  copy_instruction (:seq
                    (:alias (:pattern "[cC][oO][pP][yY]") "COPY")
                    (:repeat param)
                    (:repeat1 (:seq (:alias path_with_heredoc path) _non_newline_whitespace))
                    (:alias path_with_heredoc path)
                    (:repeat heredoc_block))
  entrypoint_instruction (:seq
                          (:alias
                           (:pattern "[eE][nN][tT][rR][yY][pP][oO][iI][nN][tT]")
                           "ENTRYPOINT")
                          (:choice json_string_array shell_command))
  volume_instruction (:seq
                      (:alias (:pattern "[vV][oO][lL][uU][mM][eE]") "VOLUME")
                      (:choice
                       json_string_array
                       (:seq path (:repeat (:seq _non_newline_whitespace path)))))
  user_instruction (:seq
                    (:alias (:pattern "[uU][sS][eE][rR]") "USER")
                    (:field :user (:alias _user_name_or_group unquoted_string))
                    (:choice
                     (:seq
                      (:token-immediate ":")
                      (:field :group (:alias _immediate_user_name_or_group unquoted_string)))
                     :blank))
  _user_name_or_group (:seq
                       (:choice (:pattern "([a-zA-Z][-A-Za-z0-9_]*|[0-9]+)") expansion)
                       (:repeat _immediate_user_name_or_group_fragment))
  _immediate_user_name_or_group (:repeat1 _immediate_user_name_or_group_fragment)
  _immediate_user_name_or_group_fragment (:choice
                                          (:token-immediate
                                           (:pattern "([a-zA-Z][-a-zA-Z0-9_]*|[0-9]+)"))
                                          _immediate_expansion)
  workdir_instruction (:seq (:alias (:pattern "[wW][oO][rR][kK][dD][iI][rR]") "WORKDIR") path)
  arg_instruction (:seq
                   (:alias (:pattern "[aA][rR][gG]") "ARG")
                   (:field :name (:alias (:pattern "[a-zA-Z0-9_]+") unquoted_string))
                   (:choice
                    (:seq
                     (:token-immediate "=")
                     (:field :default
                      (:choice double_quoted_string single_quoted_string unquoted_string)))
                    :blank))
  onbuild_instruction (:seq
                       (:alias (:pattern "[oO][nN][bB][uU][iI][lL][dD]") "ONBUILD")
                       _instruction)
  stopsignal_instruction (:seq
                          (:alias
                           (:pattern "[sS][tT][oO][pP][sS][iI][gG][nN][aA][lL]")
                           "STOPSIGNAL")
                          _stopsignal_value)
  _stopsignal_value (:seq
                     (:choice (:pattern "[A-Z0-9]+") expansion)
                     (:repeat
                      (:choice (:token-immediate (:pattern "[A-Z0-9]+")) _immediate_expansion)))
  healthcheck_instruction (:seq
                           (:alias
                            (:pattern "[hH][eE][aA][lL][tT][hH][cC][hH][eE][cC][kK]")
                            "HEALTHCHECK")
                           (:choice "NONE" (:seq (:repeat param) cmd_instruction)))
  shell_instruction (:seq (:alias (:pattern "[sS][hH][eE][lL][lL]") "SHELL") json_string_array)
  maintainer_instruction (:seq
                          (:alias
                           (:pattern "[mM][aA][iI][nN][tT][aA][iI][nN][eE][rR]")
                           "MAINTAINER")
                          (:pattern ".*"))
  cross_build_instruction (:seq
                           (:alias
                            (:pattern "[cC][rR][oO][sS][sS]_[bB][uU][iI][lL][dD][a-zA-Z_]*")
                            "CROSS_BUILD")
                           (:pattern ".*"))
  heredoc_block (:seq
                 (:alias heredoc_nl "_heredoc_nl")
                 (:repeat (:seq heredoc_line "\n"))
                 heredoc_end)
  path (:seq
        (:choice (:pattern "[^-\\s\\$<]") (:pattern "<[^<]") expansion)
        (:repeat (:choice (:token-immediate (:pattern "[^\\s\\$]+")) _immediate_expansion)))
  path_with_heredoc (:choice
                     heredoc_marker
                     (:seq
                      (:choice (:pattern "[^-\\s\\$<]") (:pattern "<[^-\\s\\$<]") expansion)
                      (:repeat
                       (:choice (:token-immediate (:pattern "[^\\s\\$]+")) _immediate_expansion))))
  expansion (:seq "$" _expansion_body)
  _immediate_expansion (:alias _imm_expansion expansion)
  _imm_expansion (:seq (:token-immediate "$") _expansion_body)
  _expansion_body (:choice
                   variable
                   (:seq
                    (:token-immediate "{")
                    (:alias (:token-immediate (:pattern "[^\\}]+")) variable)
                    (:token-immediate "}")))
  variable (:token-immediate (:pattern "[a-zA-Z_][a-zA-Z0-9_]*"))
  env_pair (:seq
            (:field :name _env_key)
            (:token-immediate "=")
            (:choice
             (:field :value (:choice double_quoted_string single_quoted_string unquoted_string))
             :blank))
  _spaced_env_pair (:seq
                    (:field :name _env_key)
                    (:token-immediate (:pattern "\\s+"))
                    (:field :value
                     (:choice double_quoted_string single_quoted_string unquoted_string)))
  _env_key (:alias (:pattern "[a-zA-Z_][a-zA-Z0-9_]*") unquoted_string)
  expose_port (:seq (:pattern "\\d+(-\\d+)?") (:choice (:choice "/tcp" "/udp") :blank))
  label_pair (:seq
              (:field :key
               (:choice
                (:alias (:pattern "[-a-zA-Z0-9\\._]+") unquoted_string)
                double_quoted_string
                single_quoted_string))
              (:token-immediate "=")
              (:field :value (:choice double_quoted_string single_quoted_string unquoted_string)))
  image_spec (:seq
              (:field :name image_name)
              (:seq
               (:field :tag (:choice image_tag :blank))
               (:field :digest (:choice image_digest :blank))))
  image_name (:seq
              (:choice (:pattern "[^@:\\s\\$-]") expansion)
              (:repeat (:choice (:token-immediate (:pattern "[^@:\\s\\$]+")) _immediate_expansion)))
  image_tag (:seq
             (:token-immediate ":")
             (:repeat1 (:choice (:token-immediate (:pattern "[^@\\s\\$]+")) _immediate_expansion)))
  image_digest (:seq
                (:token-immediate "@")
                (:repeat1
                 (:choice (:token-immediate (:pattern "[a-zA-Z0-9:]+")) _immediate_expansion)))
  param (:seq
         "--"
         (:field :name (:token-immediate (:pattern "[a-z][-a-z]*")))
         (:token-immediate "=")
         (:field :value (:token-immediate (:pattern "[^\\s]+"))))
  mount_param (:seq
               "--"
               (:field :name (:token-immediate "mount"))
               (:token-immediate "=")
               (:field :value
                (:seq mount_param_param (:repeat (:seq (:token-immediate ",") mount_param_param)))))
  mount_param_param (:seq
                     (:token-immediate (:pattern "[^\\s=,]+"))
                     (:token-immediate "=")
                     (:token-immediate (:pattern "[^\\s=,]+")))
  image_alias (:seq
               (:choice (:pattern "[-a-zA-Z0-9_]+") expansion)
               (:repeat
                (:choice (:token-immediate (:pattern "[-a-zA-Z0-9_]+")) _immediate_expansion)))
  shell_command (:seq
                 shell_fragment
                 (:repeat
                  (:seq (:alias required_line_continuation line_continuation) shell_fragment)))
  shell_fragment (:repeat1
                  (:choice
                   (:seq heredoc_marker (:pattern "[ \\t]*"))
                   (:pattern "[,=-]")
                   (:pattern "[^\\\\\\[\\n#\\s,=-][^\\\\\\n<]*")
                   (:pattern "\\\\[^\\n,=-]")
                   (:pattern "<[^<]")))
  line_continuation (:pattern "\\\\[ \\t]*\\n")
  required_line_continuation "\\\n"
  json_string_array (:seq
                     "["
                     (:choice (:seq json_string (:repeat (:seq "," json_string))) :blank)
                     "]")
  json_string (:seq
               "\""
               (:repeat
                (:choice
                 (:token-immediate (:pattern "[^\"\\\\]+"))
                 (:alias json_escape_sequence escape_sequence)))
               "\"")
  json_escape_sequence (:token-immediate (:pattern "\\\\(?:[\"\\\\/bfnrt]|u[0-9A-Fa-f]{4})"))
  double_quoted_string (:seq
                        "\""
                        (:repeat
                         (:choice
                          (:token-immediate (:pattern "[^\"\\n\\\\\\$]+"))
                          (:alias double_quoted_escape_sequence escape_sequence)
                          "\\"
                          _immediate_expansion))
                        "\"")
  single_quoted_string (:seq
                        "'"
                        (:repeat
                         (:choice
                          (:token-immediate (:pattern "[^'\\n\\\\]+"))
                          (:alias single_quoted_escape_sequence escape_sequence)
                          "\\"))
                        "'")
  unquoted_string (:repeat1
                   (:choice
                    (:token-immediate (:pattern "[^\\s\\n\\\"'\\\\\\$]+"))
                    (:token-immediate "\\ ")
                    _immediate_expansion))
  double_quoted_escape_sequence (:token-immediate (:choice "\\\\" "\\\""))
  single_quoted_escape_sequence (:token-immediate (:choice "\\\\" "\\'"))
  _non_newline_whitespace (:token-immediate (:pattern "[\\t ]+"))
  comment (:pattern "#.*")}}
