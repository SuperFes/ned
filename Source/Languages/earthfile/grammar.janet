# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "earthfile"
 :extras [(:pattern "[ \\t]+")
          "\n"
          "\r\n"
          "\x0c"
          line_continuation
          comment
          line_continuation_comment]
 :conflicts [[_immediate_identifier _immediate_string_base]
             [_immediate_string_base identifier]
             [_string_base]
             [_string_base identifier]
             [build_arg]
             [earthfile_ref image_name unquoted_string]
             [earthfile_ref unquoted_string]
             [image_name unquoted_string]
             [shell_fragment]
             [string]
             [shell_fragment string_array]
             [string_array unquoted_string]
             [target_artifact]
             [target_ref unquoted_string]
             [unquoted_string]
             [unquoted_string_with_spaces]
             [variable _immediate_string_base]
             [variable _string_base]]
 :precedences []
 :externals [_indent _dedent]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq
               (:choice version_command :blank)
               (:choice (:field :base_target block) :blank)
               (:repeat target))
  project_command (:seq
                   "PROJECT"
                   (:field :org_name identifier)
                   (:token-immediate "/")
                   (:field :project_name identifier)
                   _eol)
  target (:seq
          (:field :name identifier)
          ":"
          _eol
          (:choice (:seq _indent (:choice block :blank) _dedent) :blank))
  version_command (:seq
                   "VERSION"
                   (:field :options (:choice (:alias version_options options) :blank))
                   (:field :version version_major_minor)
                   _eol)
  version_options (:repeat1 feature_flag)
  arg_command (:seq
               "ARG"
               (:field :options (:choice (:alias arg_options options) :blank))
               (:field :name variable)
               (:choice
                (:seq
                 "="
                 (:field :default_value (:choice (:alias string_with_spaces string) :blank)))
                :blank)
               _eol)
  arg_options (:repeat1 (:choice required global unknown_option))
  build_command (:seq
                 "BUILD"
                 (:field :options (:choice (:alias build_options options) :blank))
                 (:choice target_ref string)
                 (:choice build_args :blank)
                 _eol)
  build_options (:repeat1
                 (:choice
                  auto_skip
                  allow_privileged
                  build_arg_deprecated
                  pass_args
                  platform
                  unknown_option))
  cache_command (:seq
                 "CACHE"
                 (:field :options (:choice (:alias cache_options options) :blank))
                 (:field :mount_point string)
                 _eol)
  cache_options (:repeat1 (:choice chmod id persist sharing unknown_option))
  cmd_command (:seq "CMD" (:choice shell_fragment string_array) _eol)
  copy_command (:seq
                "COPY"
                (:field :options (:choice (:alias copy_options options) :blank))
                (:repeat1 (:field :src (:choice target_artifact target_artifact_build_args string)))
                (:field :dest string)
                _eol)
  copy_options (:repeat1
                (:choice
                 allow_privileged
                 build_arg_deprecated
                 chmod
                 chown
                 dir
                 if_exists
                 keep_own
                 keep_ts
                 pass_args
                 platform
                 symlink_no_follow
                 unknown_option))
  do_command (:seq
              "DO"
              (:field :options (:choice (:alias do_options options) :blank))
              (:choice (:alias target_ref function_ref) string)
              (:choice build_args :blank)
              _eol)
  do_options (:repeat1 (:choice allow_privileged pass_args unknown_option))
  entrypoint_command (:seq "ENTRYPOINT" (:choice shell_fragment string_array) _eol)
  env_command (:seq
               "ENV"
               (:field :key variable)
               (:choice (:token (:prec 5 "=")) :blank)
               (:field :value (:alias string_with_spaces string))
               _eol)
  expose_command (:seq "EXPOSE" (:repeat string) _eol)
  for_command (:seq
               "FOR"
               (:field :options (:choice (:alias for_options options) :blank))
               (:field :name variable)
               "IN"
               (:field :values strings)
               _eol
               (:choice block :blank)
               "END"
               _eol)
  for_options (:repeat1 (:choice sep privileged ssh no_cache mount secret unknown_option))
  from_command (:seq
                "FROM"
                (:field :options (:choice (:alias from_options options) :blank))
                (:choice target_ref image_spec string)
                (:choice build_args :blank)
                _eol)
  from_options (:repeat1 (:choice platform allow_privileged pass_args unknown_option))
  from_dockerfile_command (:seq
                           "FROM DOCKERFILE"
                           (:field :options
                            (:choice (:alias from_dockerfile_options options) :blank))
                           (:field :context
                            (:choice target_artifact target_artifact_build_args string))
                           _eol)
  from_dockerfile_options (:repeat1
                           (:choice
                            allow_privileged
                            docker_build_arg
                            docker_file
                            docker_target
                            platform
                            unknown_option))
  function_command (:seq (:choice "FUNCTION" "COMMAND") _eol)
  git_clone_command (:seq
                     "GIT CLONE"
                     (:field :options (:choice (:alias git_clone_options options) :blank))
                     (:field :url string)
                     (:field :dest string)
                     _eol)
  git_clone_options (:repeat1 (:choice branch keep_ts unknown_option))
  host_command (:seq "HOST" (:field :name identifier) (:field :ip string) _eol)
  if_command (:seq
              "IF"
              _conditional_block
              (:repeat (:field :alternative elif_block))
              (:choice (:field :alternative else_block) :blank)
              "END"
              _eol)
  import_command (:seq
                  "IMPORT"
                  (:field :options (:choice (:alias import_options options) :blank))
                  (:choice earthfile_ref string)
                  (:choice (:seq "AS" (:field :alias identifier)) :blank)
                  _eol)
  import_options (:repeat1 (:choice allow_privileged unknown_option))
  let_command (:seq
               "LET"
               (:field :name variable)
               (:token-immediate "=")
               (:field :value (:alias string_with_spaces string))
               _eol)
  label_command (:seq "LABEL" (:repeat label) _eol)
  locally_command (:seq "LOCALLY" _eol)
  run_command (:seq
               "RUN"
               (:field :options (:choice (:alias run_options options) :blank))
               (:choice (:token (:prec 5 "-- ")) :blank)
               (:field :command (:choice string_array shell_fragment))
               _eol)
  run_options (:repeat1
               (:choice
                aws
                entrypoint
                interactive
                mount
                network_none
                no_cache
                privileged
                oidc
                push
                raw_output
                secret
                ssh
                unknown_option))
  save_artifact_command (:seq
                         "SAVE ARTIFACT"
                         (:field :options (:choice (:alias save_artifact_options options) :blank))
                         (:field :src string)
                         (:choice (:field :dest string) :blank)
                         (:choice (:seq "AS LOCAL" (:field :local_dest string)) :blank)
                         _eol)
  save_artifact_options (:repeat1
                         (:choice if_exists force keep_own keep_ts symlink_no_follow unknown_option))
  save_image_command (:seq
                      "SAVE IMAGE"
                      (:field :options (:choice (:alias save_image_options options) :blank))
                      (:choice (:field :images images) :blank)
                      _eol)
  save_image_options (:repeat1
                      (:choice cache_from cache_hint push without_earthly_labels unknown_option))
  set_command (:seq
               "SET"
               (:field :name variable)
               (:token-immediate "=")
               (:field :value (:alias string_with_spaces string))
               _eol)
  try_command (:seq
               "TRY"
               _eol
               (:choice (:field :body block) :blank)
               "FINALLY"
               _eol
               (:field :finally (:choice (:alias try_command_finally_block block) :blank))
               "END"
               _eol)
  try_command_finally_block (:repeat1 save_artifact_command)
  user_command (:seq
                "USER"
                (:choice
                 (:seq
                  (:field :user (:choice identifier number))
                  (:choice
                   (:seq (:token-immediate ":") (:field :group (:choice identifier number)))
                   :blank))
                 string)
                _eol)
  volume_command (:seq "VOLUME" (:choice string_array (:repeat1 string)) _eol)
  wait_command (:seq "WAIT" _eol (:choice block :blank) "END" _eol)
  with_docker_command (:seq
                       "WITH DOCKER"
                       (:field :options (:choice (:alias with_docker_options options) :blank))
                       _eol
                       run_command
                       "END"
                       _eol)
  with_docker_options (:repeat1
                       (:choice
                        allow_privileged
                        cache_id
                        compose
                        load
                        platform
                        pull
                        service
                        unknown_option))
  workdir_command (:seq "WORKDIR" string _eol)
  block (:repeat1
         (:choice
          (:pattern "\\s+")
          arg_command
          build_command
          cache_command
          cmd_command
          copy_command
          do_command
          entrypoint_command
          env_command
          expose_command
          for_command
          from_command
          from_dockerfile_command
          function_command
          git_clone_command
          host_command
          if_command
          import_command
          let_command
          label_command
          locally_command
          project_command
          run_command
          save_artifact_command
          save_image_command
          set_command
          try_command
          user_command
          volume_command
          wait_command
          with_docker_command
          workdir_command))
  _conditional_block (:seq
                      (:field :options (:choice (:alias _conditional_block_options options) :blank))
                      (:field :condition shell_fragment)
                      _eol
                      (:field :body (:choice block :blank)))
  _conditional_block_options (:repeat1
                              (:choice ssh privileged no_cache mount secret unknown_option))
  elif_block (:seq "ELSE IF" _conditional_block)
  else_block (:seq "ELSE" (:field :body block))
  _immediate_identifier (:seq
                         _immediate_string_base_alpha
                         (:repeat
                          (:choice
                           _immediate_string_base_alpha
                           _immediate_string_base_num
                           (:token-immediate ".")
                           (:token-immediate "-"))))
  earthfile_ref (:seq
                 (:choice _string_base "(" ")" "[" "]" "{" "}" "/" "," ":" "@" "." "-")
                 (:repeat
                  (:prec-left 0
                   (:choice
                    _immediate_string_base
                    (:token-immediate "(")
                    (:token-immediate ")")
                    (:token-immediate "[")
                    (:token-immediate "]")
                    (:token-immediate "{")
                    (:token-immediate "}")
                    (:token-immediate "/")
                    (:token-immediate ",")
                    (:token-immediate ":")
                    (:token-immediate "@")
                    (:token-immediate ".")
                    (:token-immediate "-")
                    (:alias _immediate_escape_sequence escape_sequence)))))
  function_ref "dummy node to be used as an alias for target_ref"
  identifier (:seq
              _string_base_alpha
              (:repeat
               (:choice
                _immediate_string_base_alpha
                _immediate_string_base_num
                (:token-immediate ".")
                (:token-immediate "-"))))
  image_spec (:seq
              (:field :name image_name)
              (:choice (:seq (:token-immediate ":") (:field :tag image_tag)) :blank)
              (:choice (:seq (:token-immediate "@") (:field :digest image_digest)) :blank))
  image_name (:seq
              _string_base
              (:repeat
               (:prec-left 0
                (:choice
                 _immediate_string_base
                 (:token-immediate "/")
                 (:token-immediate "-")
                 (:token-immediate ".")))))
  image_tag (:seq
             (:choice _immediate_string_base_alpha _immediate_string_base_num)
             (:repeat
              (:choice
               _immediate_string_base_alpha
               _immediate_string_base_num
               (:token-immediate ".")
               (:token-immediate "-"))))
  image_digest (:seq
                (:choice _immediate_string_base_alpha _immediate_string_base_num)
                (:repeat
                 (:choice
                  _immediate_string_base_alpha
                  _immediate_string_base_num
                  (:token-immediate ":"))))
  images (:repeat1 (:choice image_spec string))
  label (:seq
         (:field :label identifier)
         (:choice (:token-immediate " ") (:token-immediate "="))
         (:field :value string))
  number (:pattern "\\d+")
  options "dummy node to use as an alias in the command options"
  shell_fragment (:repeat1
                  (:choice
                   _string_base
                   comment
                   "("
                   ")"
                   "["
                   "]"
                   "{"
                   "}"
                   "$"
                   "/"
                   ","
                   ":"
                   "@"
                   "="
                   "+"
                   "."
                   "-"
                   (:alias escape_sequence _immediate_escape_sequence)
                   (:seq
                    "\""
                    (:repeat
                     (:choice
                      (:token-immediate (:prec 15 (:pattern "[^\"\\\\]+")))
                      _immediate_escape_sequence))
                    "\"")
                   (:seq
                    "'"
                    (:repeat
                     (:choice
                      (:token-immediate (:prec 15 (:pattern "[^'\\\\]+")))
                      _immediate_escape_sequence))
                    "'")))
  string_array (:prec-dynamic 10
                (:choice
                 (:seq "[" "]")
                 (:seq "[" (:repeat (:seq double_quoted_string ",")) double_quoted_string "]")))
  target_ref (:seq
              (:choice (:field :earthfile earthfile_ref) :blank)
              "+"
              (:field :name (:alias _immediate_identifier identifier)))
  target_ref_with_build_args (:seq
                              (:token (:prec 5 "("))
                              target_ref
                              build_args
                              (:token (:prec 5 ")")))
  target_artifact (:seq
                   target_ref
                   (:token-immediate "/")
                   (:choice (:alias _immediate_unquoted_string unquoted_string) :blank))
  target_artifact_build_args (:seq
                              (:token (:prec 5 "("))
                              (:choice
                               (:seq
                                target_ref
                                (:token-immediate "/")
                                (:alias _immediate_unquoted_string unquoted_string))
                               string)
                              (:choice build_args :blank)
                              (:token (:prec 5 ")")))
  variable (:seq
            _string_base_alpha
            (:repeat (:choice _immediate_string_base_alpha _immediate_string_base_num)))
  version_major_minor (:pattern "[0-9]+\\.[0-9]+")
  allow_privileged (:token (:prec 5 "--allow-privileged"))
  auto_skip (:token (:prec 5 "--auto-skip"))
  aws (:token (:prec 5 "--aws"))
  branch (:seq
          (:token (:prec 5 "--branch"))
          (:choice (:token-immediate " ") (:token-immediate "="))
          (:field :value string))
  build_arg (:seq
             (:choice (:token (:prec 5 "--")) "-")
             (:field :name (:alias _immediate_variable variable))
             (:choice
              (:seq (:token-immediate "=") (:choice (:field :value string) :blank))
              (:field :value string)))
  build_args (:repeat1 build_arg)
  build_arg_deprecated (:seq
                        (:token (:prec 5 "--build-arg"))
                        (:choice (:token-immediate " ") (:token-immediate "="))
                        (:field :value string))
  cache_id (:seq
            (:token (:prec 5 "--cache-id"))
            (:choice (:token-immediate " ") (:token-immediate "="))
            (:field :value string))
  cache_from (:seq
              (:token (:prec 5 "--cache-from"))
              (:choice (:token-immediate " ") (:token-immediate "="))
              (:field :value string))
  cache_hint (:token (:prec 5 "--cache-hint"))
  chmod (:seq
         (:token (:prec 5 "--chmod"))
         (:choice (:token-immediate " ") (:token-immediate "="))
         (:field :value string))
  chown (:seq
         (:token (:prec 5 "--chown"))
         (:choice (:token-immediate " ") (:token-immediate "="))
         (:field :value string))
  compose (:seq
           (:token (:prec 5 "--compose"))
           (:choice (:token-immediate " ") (:token-immediate "="))
           (:field :value string))
  dir (:token (:prec 5 "--dir"))
  docker_build_arg (:seq
                    (:token (:prec 5 "--build-arg"))
                    (:choice (:token-immediate " ") (:token-immediate "="))
                    (:field :key identifier)
                    (:choice (:token-immediate " ") (:token-immediate "="))
                    (:field :value string))
  docker_file (:seq
               (:token (:prec 5 "-f"))
               (:choice (:token-immediate " ") (:token-immediate "="))
               (:field :value (:choice target_artifact target_artifact_build_args string)))
  docker_target (:seq
                 (:token (:prec 5 "--target"))
                 (:choice (:token-immediate " ") (:token-immediate "="))
                 (:field :value identifier))
  entrypoint (:token (:prec 5 "--entrypoint"))
  feature_flag (:pattern "--[a-zA-Z0-9\\-]+")
  force (:token (:prec 5 "--force"))
  global (:token (:prec 5 "--global"))
  id (:seq
      (:token (:prec 5 "--id"))
      (:choice (:token-immediate " ") (:token-immediate "="))
      identifier)
  if_exists (:token (:prec 5 "--if-exists"))
  interactive (:choice (:token (:prec 5 "--interactive")) (:token (:prec 5 "--interactive-keep")))
  keep_own (:token (:prec 5 "--keep-own"))
  keep_ts (:token (:prec 5 "--keep-ts"))
  load (:seq
        (:token (:prec 5 "--load"))
        (:choice (:token-immediate " ") (:token-immediate "="))
        (:choice (:seq (:field :image (:choice image_spec string)) (:token-immediate "=")) :blank)
        (:field :target (:choice target_ref target_ref_with_build_args string)))
  mount (:seq
         (:token (:prec 5 "--mount"))
         (:choice (:token-immediate " ") (:token-immediate "="))
         (:field :value string))
  network_none (:token (:prec 5 "--network=none"))
  no_cache (:token (:prec 5 "--no-cache"))
  oidc (:seq
        (:token (:prec 5 "--oidc"))
        (:choice (:token-immediate " ") (:token-immediate "="))
        (:field :spec string))
  pass_args (:token (:prec 5 "--pass-args"))
  persist (:token (:prec 5 "--persist"))
  platform (:seq
            (:token (:prec 5 "--platform"))
            (:choice (:token-immediate " ") (:token-immediate "="))
            (:field :value string))
  privileged (:token (:prec 5 "--privileged"))
  pull (:seq
        (:token (:prec 5 "--pull"))
        (:choice (:token-immediate " ") (:token-immediate "="))
        (:field :value (:choice image_spec string)))
  push (:token (:prec 5 "--push"))
  raw_output (:token (:prec 5 "--raw-output"))
  required (:token (:prec 5 "--required"))
  secret (:seq
          (:token (:prec 5 "--secret"))
          (:choice (:token-immediate " ") (:token-immediate "="))
          (:choice
           (:field :id string)
           (:seq
            (:field :var (:choice variable string))
            (:token-immediate (:prec 5 "="))
            (:field :id string))))
  sep (:seq
       (:token (:prec 5 "--sep"))
       (:choice (:token-immediate " ") (:token-immediate "="))
       (:field :value string))
  service (:seq
           (:token (:prec 5 "--service"))
           (:choice (:token-immediate " ") (:token-immediate "="))
           (:field :value string))
  sharing (:seq
           (:token (:prec 5 "--sharing"))
           (:choice (:token-immediate " ") (:token-immediate "="))
           identifier)
  ssh (:token (:prec 5 "--ssh"))
  symlink_no_follow (:token (:prec 5 "--symlink-no-follow"))
  unknown_option (:seq
                  (:token (:prec 3 (:pattern "--[a-z0-9-]*")))
                  (:choice (:seq (:token-immediate "=") (:field :value string)) :blank))
  without_earthly_labels (:token (:prec 5 "--without-earthly-labels"))
  _string_base (:seq
                (:choice _string_base_other _string_base_alpha _string_base_num)
                (:choice _immediate_string_base :blank))
  _string_base_other (:pattern "[^\"'\\s\\\\\\$()\\[\\]{}+,:@=a-zA-Z0-9_/.-]+")
  _string_base_alpha (:pattern "[a-zA-Z_]+")
  _string_base_num (:pattern "[0-9]+")
  _immediate_string_base (:prec 2
                          (:repeat1
                           (:choice
                            _immediate_string_base_other
                            _immediate_string_base_alpha
                            _immediate_string_base_num)))
  _immediate_string_base_other (:token-immediate
                                (:pattern "[^\"'\\s\\\\\\$()\\[\\]{}+,:@=a-zA-Z0-9_/.-]+"))
  _immediate_string_base_alpha (:token-immediate (:pattern "[a-zA-Z_]+"))
  _immediate_string_base_num (:token-immediate (:pattern "[0-9]+"))
  double_quoted_string (:prec-dynamic -1
                        (:seq
                         "\""
                         (:repeat
                          (:choice
                           (:token-immediate (:prec 15 (:pattern "[^\"\\\\\\$]+")))
                           (:alias _immediate_escape_sequence escape_sequence)
                           (:alias _immediate_expansion expansion)))
                         "\""))
  _immediate_double_quoted_string (:prec 2
                                   (:seq
                                    (:token-immediate "\"")
                                    (:repeat
                                     (:choice
                                      (:token-immediate (:prec 15 (:pattern "[^\"\\\\\\$]+")))
                                      (:alias _immediate_escape_sequence escape_sequence)
                                      (:alias _immediate_expansion expansion)))
                                    (:token-immediate "\"")))
  single_quoted_string (:prec-dynamic -1
                        (:seq
                         (:token-immediate "'")
                         (:repeat
                          (:choice
                           (:token-immediate (:pattern "[^'\\n\\\\]+"))
                           (:alias _immediate_escape_sequence escape_sequence)))
                         (:token-immediate "'")))
  _immediate_single_quoted_string (:prec 2
                                   (:seq
                                    "'"
                                    (:repeat
                                     (:choice
                                      (:token-immediate (:pattern "[^'\\n\\\\]+"))
                                      (:alias _immediate_escape_sequence escape_sequence)))
                                    "'"))
  unquoted_string (:prec-dynamic -1
                   (:seq
                    (:choice
                     _string_base
                     expansion
                     escape_sequence
                     "("
                     ")"
                     "["
                     "]"
                     "{"
                     "}"
                     "/"
                     ","
                     ":"
                     "@"
                     "="
                     "+"
                     "."
                     "-")
                    (:repeat
                     (:choice
                      _immediate_string_base
                      (:alias _immediate_escape_sequence escape_sequence)
                      (:token-immediate "(")
                      (:token-immediate ")")
                      (:token-immediate "[")
                      (:token-immediate "]")
                      (:token-immediate "{")
                      (:token-immediate "}")
                      (:token-immediate "/")
                      (:token-immediate ",")
                      (:token-immediate ":")
                      (:token-immediate "@")
                      (:token-immediate "=")
                      (:token-immediate "+")
                      (:token-immediate ".")
                      (:token-immediate "-")
                      (:alias _immediate_expansion expansion)))))
  _immediate_unquoted_string (:prec 2
                              (:repeat1
                               (:choice
                                _immediate_string_base
                                (:alias _immediate_escape_sequence escape_sequence)
                                (:token-immediate "(")
                                (:token-immediate ")")
                                (:token-immediate "[")
                                (:token-immediate "]")
                                (:token-immediate "{")
                                (:token-immediate "}")
                                (:token-immediate "/")
                                (:token-immediate ",")
                                (:token-immediate ":")
                                (:token-immediate "@")
                                (:token-immediate "=")
                                (:token-immediate "+")
                                (:token-immediate ".")
                                (:token-immediate "-")
                                (:alias _immediate_expansion expansion))))
  unquoted_string_with_spaces (:prec-dynamic -1
                               (:seq
                                (:choice
                                 _string_base
                                 expansion
                                 escape_sequence
                                 "("
                                 ")"
                                 "["
                                 "]"
                                 "{"
                                 "}"
                                 "/"
                                 ","
                                 ":"
                                 "@"
                                 "="
                                 "+"
                                 "."
                                 "-")
                                (:repeat
                                 (:choice
                                  _immediate_string_base
                                  (:alias _immediate_escape_sequence escape_sequence)
                                  (:token-immediate "(")
                                  (:token-immediate ")")
                                  (:token-immediate "[")
                                  (:token-immediate "]")
                                  (:token-immediate "{")
                                  (:token-immediate "}")
                                  (:token-immediate "/")
                                  (:token-immediate ",")
                                  (:token-immediate ":")
                                  (:token-immediate "@")
                                  (:token-immediate "=")
                                  (:token-immediate "+")
                                  (:token-immediate ".")
                                  (:token-immediate "-")
                                  (:token-immediate (:pattern "[ \\t]+"))
                                  (:alias _immediate_expansion expansion)))))
  string_with_spaces (:prec-dynamic -1
                      (:seq
                       (:choice
                        (:alias unquoted_string_with_spaces unquoted_string)
                        double_quoted_string
                        single_quoted_string)
                       (:repeat
                        (:choice
                         (:alias unquoted_string_with_spaces unquoted_string)
                         double_quoted_string
                         single_quoted_string))))
  string (:prec-dynamic -1
          (:seq
           (:choice unquoted_string double_quoted_string single_quoted_string)
           (:repeat
            (:choice
             (:alias _immediate_unquoted_string unquoted_string)
             (:alias _immediate_double_quoted_string double_quoted_string)
             (:alias _immediate_single_quoted_string single_quoted_string)))))
  strings (:repeat1 string)
  expansion (:seq
             "$"
             (:choice
              (:alias _immediate_variable variable)
              (:seq (:token-immediate "{") (:alias _immediate_variable variable) "}")
              (:seq (:token-immediate "(") shell_fragment (:token (:prec 10 ")")))))
  _immediate_expansion (:seq
                        (:token-immediate "$")
                        (:choice
                         (:alias _immediate_variable variable)
                         (:seq
                          (:token-immediate "{")
                          (:alias _immediate_variable variable)
                          (:token-immediate "}"))
                         (:seq (:token-immediate "(") shell_fragment (:token-immediate ")"))))
  _immediate_variable (:prec-left 2
                       (:seq
                        _immediate_string_base_alpha
                        (:repeat (:choice _immediate_string_base_alpha _immediate_string_base_num))))
  _immediate_escape_sequence (:pattern "\\\\.")
  escape_sequence (:pattern "\\\\.")
  line_continuation (:token (:prec 10 "\\\n"))
  comment (:token (:prec 10 (:pattern "#[^\\n]*(\\n|\\r\\n|\\f)")))
  line_continuation_comment (:token (:prec 10 (:pattern "\\\\(\\s*#.*\\n)+")))
  _eol (:choice "\n" "\r\n" "\x0c" "\0" comment)}}
