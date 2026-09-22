# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "nginx"
 :extras [(:pattern "\\s") comment]
 :conflicts [[file mask]]
 :precedences []
 :externals [_newline _indent _dedent]
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat (:choice directive block if map attribute location))
  comment (:prec-left 0 (:token (:seq "#" (:pattern ".*"))))
  _body (:repeat1 (:choice directive block if map attribute location))
  random_value (:token (:prec -1 (:pattern "[^;\\s]*")))
  _attribute_value (:choice
                    quoted_string_literal
                    string_literal
                    auto
                    level
                    boolean
                    connection_method
                    size
                    time
                    numeric_literal
                    (:alias random_value value))
  attribute (:seq
             (:alias (:choice _word "''") keyword)
             (:choice
              block
              (:seq _attribute_value (:repeat (:seq (:pattern "\\s") _attribute_value)) ";"))
             _newline)
  condition (:token (:seq "(" (:repeat (:pattern "[^)]|(\\\\\\))")) ")"))
  if (:seq "if" (:field :condition condition) block)
  location_route (:token (:prec -1 (:pattern "[^{]+")))
  location_modifier (:choice "=" "~" "~*" "^~")
  location (:seq "location" (:choice location_modifier :blank) location_route block)
  directive (:choice
             _boolean_directive
             _time_directive
             _number_directive
             _number_or_auto_directive
             _debug_points_directive
             _file_directive
             _include_directive
             _use_directive
             _working_directory_directive
             _working_core_directive
             _affinity_directive
             _abstract_directive
             _env_directive
             _error_log_directive
             _thread_pool_directive
             _user_directive
             _events_directive
             _return_directive)
  block (:seq "{" (:choice _newline :blank) (:choice _body :blank) "}")
  on "on"
  off "off"
  boolean (:choice on off)
  auto "auto"
  cpumask (:token (:pattern "[01]+"))
  connection_method (:choice "select" "poll" "kqueue" "epoll" "/dev/poll" "eventport")
  level (:choice "debug" "info" "notice" "warn" "error" "crit" "alert" "emerg")
  time (:token (:seq (:repeat1 (:pattern "[0-9]")) (:pattern "(ms|s|m|h|d|w|M|y)")))
  size (:token (:seq (:repeat1 (:pattern "[0-9]")) (:pattern "[mkgMKG]")))
  _fileish (:choice (:pattern "[0-9]") (:pattern "\\p{L}") "/" "." "-" "_")
  file (:prec-left 0 (:seq _fileish (:repeat _fileish)))
  mask (:prec-right 0 (:seq (:choice _fileish "*") (:repeat (:choice _fileish "*"))))
  _word (:token
         (:seq
          (:pattern "\\p{L}")
          (:repeat (:choice (:pattern "\\p{L}") (:pattern "[0-9]") "-" "_"))))
  var (:token
       (:seq
        (:choice (:pattern "\\p{L}") "$")
        (:repeat (:choice (:pattern "\\p{L}") (:pattern "[0-9]") "_" "$"))))
  quoted_string_literal (:prec-right 0
                         (:token (:seq "'" (:repeat (:pattern "[^']|(\\\\\\')")) "'")))
  string_literal (:token (:seq "\"" (:repeat (:pattern "[^\"]|(\\\\\\\")")) "\""))
  numeric_literal (:token
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice (:seq "." (:repeat1 (:pattern "[0-9]"))) :blank)
                    (:choice
                     (:seq
                      (:choice "e" "E")
                      (:choice (:choice "+" "-") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))
  map (:seq "map" (:repeat1 (:choice var _word)) block)
  _boolean_directive (:seq (:alias _boolean_keyword keyword) boolean ";" _newline)
  _boolean_keyword (:choice "accept_mutex" "daemon" "master_process" "multi_accept" "pcre_jit")
  _time_directive (:seq (:alias _time_keyword keyword) time ";" _newline)
  _time_keyword (:choice "accept_mutex_delay" "worker_shutdown_timeout" "timer_resolution")
  _number_directive (:seq (:alias _number_keyword keyword) numeric_literal ";" _newline)
  _number_keyword (:choice
                   "worker_aio_requests"
                   "worker_connections"
                   "worker_priority"
                   "worker_rlimit_nofile")
  _file_directive (:seq (:alias _file_keyword keyword) file ";" _newline)
  _file_keyword (:choice "load_module" "lock_file" "pid")
  _include_directive (:seq (:alias "include" keyword) (:choice file mask) ";" _newline)
  _number_or_auto_directive (:seq
                             (:alias "worker_processes" keyword)
                             (:choice numeric_literal auto)
                             ";"
                             _newline)
  _debug_points_directive (:seq
                           (:alias "debug_points" keyword)
                           (:alias (:choice "abort" "stop") constant)
                           ";"
                           _newline)
  _use_directive (:seq (:alias "use" keyword) (:alias connection_method constant) ";" _newline)
  _working_directory_directive (:seq (:alias "working_directory" keyword) file ";" _newline)
  _working_core_directive (:seq (:alias "worker_rlimit_core" keyword) size ";" _newline)
  _affinity_directive (:seq
                       (:alias "worker_cpu_affinity" keyword)
                       (:seq (:choice auto cpumask) (:repeat (:choice auto cpumask)))
                       ";"
                       _newline)
  _abstract_directive (:seq
                       (:alias (:choice "debug_connection" "ssl_engine") keyword)
                       (:alias (:pattern "[^;]+") value)
                       ";"
                       _newline)
  _return_directive (:seq
                     (:alias "return" keyword)
                     numeric_literal
                     (:choice (:alias random_value value) :blank)
                     ";"
                     _newline)
  _env_directive (:seq
                  (:alias "env" keyword)
                  (:alias (:pattern "[A-Z][A-Z0-9_]+") variable)
                  (:choice (:seq "=" (:alias (:pattern "[^;]+") value)) :blank)
                  ";"
                  _newline)
  _error_log_directive (:seq
                        (:alias "error_log" keyword)
                        file
                        (:choice (:seq (:pattern "\\s") level) :blank)
                        ";"
                        _newline)
  _thread_poll_variable (:seq (:alias (:choice "threads" "max_queue") keyword) "=" numeric_literal)
  _thread_pool_directive (:seq
                          (:alias "thread_pool" keyword)
                          (:alias _word value)
                          (:alias _thread_poll_variable variable)
                          (:choice
                           (:seq (:pattern "\\s") (:alias _thread_poll_variable variable))
                           :blank)
                          ";"
                          _newline)
  _user_directive (:seq
                   (:alias "user" keyword)
                   (:alias _word value)
                   (:choice (:alias _word value) :blank)
                   ";"
                   _newline)
  _events_directive (:seq (:alias "events" keyword) block (:choice _newline :blank))}}
