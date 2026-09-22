# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "caddyfile"
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals [heredoc_start heredoc_body heredoc_end]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq
               (:choice global_options :blank)
               (:repeat (:choice snippet_definition named_route))
               (:choice
                (:choice
                 single_site
                 (:seq site_block (:repeat (:choice site_block snippet_definition named_route))))
                :blank))
  global_options (:seq "{" (:token-immediate (:pattern "\\r?\\n|\\r")) (:repeat directive) "}")
  snippet_name (:token (:seq "(" (:pattern "[a-zA-Z0-9\\-_]+") ")"))
  snippet_definition (:seq (:field :name snippet_name) block)
  named_route_identifier (:token (:seq "&(" (:pattern "[a-zA-Z0-9\\-_]+") ")"))
  named_route (:seq (:field :name named_route_identifier) block)
  _ipv4_address (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
  _ipv6_address (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
  _ip_address (:choice
               (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
               (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))"))
  _ipv4_cidr (:token
              (:seq
               (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
               (:token-immediate "/")
               (:token-immediate (:pattern "[0-9]|1[0-9]|2[0-9]|3[0-2]"))))
  _ipv6_cidr (:token
              (:seq
               (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
               (:token-immediate "/")
               (:token-immediate (:pattern "[0-9][0-9]?|1[01][0-9]|12[0-8]"))))
  _ip_cidr (:choice
            (:token
             (:seq
              (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
              (:token-immediate "/")
              (:token-immediate (:pattern "[0-9]|1[0-9]|2[0-9]|3[0-2]"))))
            (:token
             (:seq
              (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
              (:token-immediate "/")
              (:token-immediate (:pattern "[0-9][0-9]?|1[01][0-9]|12[0-8]")))))
  ip_address_or_cidr (:choice
                      (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                      (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                      (:token
                       (:seq
                        (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                        (:token-immediate "/")
                        (:token-immediate (:pattern "[0-9]|1[0-9]|2[0-9]|3[0-2]"))))
                      (:token
                       (:seq
                        (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                        (:token-immediate "/")
                        (:token-immediate (:pattern "[0-9][0-9]?|1[01][0-9]|12[0-8]")))))
  network_address (:choice
                   (:token
                    (:seq
                     (:choice
                      (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                      (:seq
                       "["
                       (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                       (:choice (:seq "%" (:pattern "[a-z0-9]+")) :blank)
                       "]")
                      (:seq
                       (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")
                       (:repeat (:seq "." (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")))
                       "."
                       (:choice (:pattern "[a-z][a-z]+") (:pattern "xn--[a-z0-9]+"))))
                     (:choice (:seq ":" (:pattern "[0-9]{1,5}")) :blank)
                     (:repeat
                      (:seq "/" (:pattern "([A-Za-z0-9\\-_.~!&'\\(\\)*+,;=:#]|%[0-9a-fA-F]{2})*")))))
                   (:token
                    (:seq
                     (:choice "http" "https" "h2c")
                     "://"
                     (:choice
                      (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                      (:seq
                       "["
                       (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                       (:choice (:seq "%" (:pattern "[a-z0-9]+")) :blank)
                       "]")
                      (:seq
                       (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")
                       (:repeat (:seq "." (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")))
                       (:choice
                        (:seq "." (:choice (:pattern "[a-z][a-z]+") (:pattern "xn--[a-z0-9]+")))
                        :blank)))
                     (:choice (:seq ":" (:pattern "[0-9]{1,5}")) :blank)
                     (:repeat
                      (:seq "/" (:pattern "([A-Za-z0-9\\-_.~!&'\\(\\)*+,;=:#]|%[0-9a-fA-F]{2})*")))))
                   (:token
                    (:seq
                     (:field :network (:choice "fd" "fdgram"))
                     "/"
                     (:field :address (:pattern "[0-9]+"))))
                   (:token
                    (:seq
                     (:field :network (:choice "unix" "unix+h2c" "unixgram" "unixpacket"))
                     "/"
                     (:field :address (:pattern "\\/[a-zA-Z0-9_\\-./*]+"))
                     (:choice (:seq "|" (:field :perms (:pattern "[0-9]{3,4}"))) :blank)))
                   (:token
                    (:seq
                     (:field :network
                      (:choice "ip" "ip4" "ip6" "tcp" "tcp4" "tcp6" "udp" "udp4" "udp6"))
                     "/"
                     (:field :address
                      (:seq
                       (:choice
                        (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                        (:seq
                         "["
                         (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                         (:choice (:seq "%" (:pattern "[a-z0-9]+")) :blank)
                         "]")
                        (:seq
                         (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")
                         (:repeat (:seq "." (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")))
                         (:choice
                          (:seq "." (:choice (:pattern "[a-z][a-z]+") (:pattern "xn--[a-z0-9]+")))
                          :blank)))
                       (:choice (:seq ":" (:pattern "[0-9]{1,5}")) :blank)))))
                   (:token
                    (:field :address
                     (:seq
                      (:seq
                       (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")
                       (:repeat (:seq "." (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")))
                       (:choice
                        (:seq "." (:choice (:pattern "[a-z][a-z]+") (:pattern "xn--[a-z0-9]+")))
                        :blank))
                      ":"
                      (:pattern "[0-9]{1,5}")))))
  site_address (:choice
                "http://"
                "https://"
                (:token (:seq ":" (:pattern "[0-9]{1,5}")))
                (:seq (:choice ":" :blank) _environment_variable)
                (:seq _environment_variable (:token-immediate (:seq ":" (:pattern "[0-9]{1,5}"))))
                _environment_variable
                (:token
                 (:seq
                  (:choice (:pattern "[a-z]+:\\/\\/") :blank)
                  (:choice
                   (:pattern "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
                   (:seq
                    "["
                    (:pattern "(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))")
                    (:choice (:seq "%" (:pattern "[a-z0-9]+")) :blank)
                    "]")
                   (:seq
                    (:choice "*" (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+"))
                    (:repeat (:seq "." (:pattern "[a-z][a-z0-9\\-]*[a-z0-9]+")))
                    (:choice
                     (:seq "." (:choice (:pattern "[a-z][a-z]+") (:pattern "xn--[a-z0-9]+")))
                     :blank)))
                  (:choice (:seq ":" (:pattern "[0-9]{1,5}")) :blank))))
  _string_literal (:choice raw_string_literal interpreted_string_literal)
  raw_string_literal (:seq "`" (:repeat _raw_string_literal_basic_content) (:token-immediate "`"))
  _raw_string_literal_basic_content (:token-immediate (:prec 1 (:pattern "[^`\\n]+")))
  interpreted_string_literal (:seq
                              "\""
                              (:repeat
                               (:choice _interpreted_string_literal_basic_content escape_sequence))
                              (:token-immediate "\""))
  _interpreted_string_literal_basic_content (:token-immediate (:prec 1 (:pattern "[^\"\\n\\\\]+")))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xuU]")
                     (:pattern "\\d{2,3}")
                     (:pattern "x[0-9a-fA-F]{2,}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "U[0-9a-fA-F]{8}"))))
  int_literal (:token (:choice "0" (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9]")))))
  duration_literal (:token
                    (:seq
                     (:choice "0" (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9]"))))
                     (:pattern "(ns|us|µs|ms|s|m|h|d)")))
  comment (:token (:seq "#" (:pattern ".*")))
  status_code_fallback (:token (:seq "=" (:pattern "[0-9]{3}")))
  placeholder _placeholder
  _placeholder (:token
                (:seq
                 "{"
                 (:token-immediate (:pattern "[a-zA-Z0-9][a-zA-Z0-9_.\\[\\]]*"))
                 (:choice
                  (:token
                   (:seq
                    "{$"
                    (:token-immediate (:pattern "[a-zA-Z0-9][a-zA-Z0-9_.\\[\\]]*"))
                    (:choice (:seq ":" (:pattern "[^}\\n\\r]+")) :blank)
                    "}"))
                  :blank)
                 "}"))
  environment_variable _environment_variable
  _environment_variable (:token
                         (:seq
                          "{$"
                          (:token-immediate (:pattern "[a-zA-Z0-9][a-zA-Z0-9_.\\[\\]]*"))
                          (:choice (:seq ":" (:pattern "[^}\\n\\r]+")) :blank)
                          "}"))
  directive_name (:pattern "[a-zA-Z_\\-+]+")
  directive (:seq
             (:field :name directive_name)
             (:choice matcher :blank)
             (:repeat
              (:choice
               network_address
               environment_variable
               placeholder
               _string_literal
               duration_literal
               int_literal
               status_code_fallback
               argument
               heredoc))
             (:choice block (:token-immediate (:pattern "\\r?\\n|\\r"))))
  path (:token (:seq (:choice "/" "\\") (:pattern "([a-zA-Z0-9\\-_%\\\\\\/.]+)*(\\*)?")))
  matcher_name (:pattern "[a-zA-Z0-9\\-_]+")
  matcher_identifier (:seq "@" (:field :name matcher_name))
  argument (:token
            (:choice
             (:pattern "[\\^a-zA-Z\\-_%+.\\\\\\/*:$0-9|\\(\\)\\[\\]?+*][a-zA-Z\\-_%+.\\\\\\/*:$0-9@|\\(\\)\\[\\]?+*\\{\\}]*")
             (:seq
              "@"
              (:pattern "[\\^a-zA-Z\\-_%+.\\\\\\/*:$0-9|\\(\\)\\[\\]?+*]*@[a-zA-Z\\-_%+.\\\\\\/*:$0-9@|\\(\\)\\[\\]?+*\\{\\}]*"))))
  _bare_cel_expression (:repeat1 _bare_cel_expression_content)
  _bare_cel_expression_content (:token-immediate (:prec 1 (:pattern "[^\\n]+")))
  _quoted_cel_expression (:prec 2 (:repeat1 _quoted_cel_expression_content))
  _quoted_cel_expression_content (:token-immediate (:prec 1 (:pattern "[^`\\n]+")))
  matcher_block (:seq
                 "{"
                 (:token-immediate (:pattern "\\r?\\n|\\r"))
                 (:field :body (:repeat matcher_directive))
                 "}")
  matcher_directive_name (:seq (:choice "not" :blank) (:pattern "[a-zA-Z_+]+"))
  matcher_directive (:seq
                     (:choice
                      (:seq
                       "`"
                       (:field :expression (:alias _quoted_cel_expression cel_expression))
                       (:token-immediate "`"))
                      (:seq
                       "expression"
                       (:choice
                        (:seq
                         (:token (:prec 2 "`"))
                         (:field :expression (:alias _quoted_cel_expression cel_expression))
                         (:token-immediate "`"))
                        (:field :expression (:alias _bare_cel_expression cel_expression))))
                      (:seq
                       (:field :name matcher_directive_name)
                       (:choice
                        matcher_block
                        (:repeat1
                         (:choice
                          network_address
                          environment_variable
                          placeholder
                          path
                          _string_literal
                          duration_literal
                          int_literal
                          argument
                          heredoc
                          ip_address_or_cidr)))))
                     (:token-immediate (:pattern "\\r?\\n|\\r")))
  named_matcher (:seq matcher_identifier (:choice matcher_block matcher_directive))
  matcher (:choice "*" (:alias path path_matcher) matcher_identifier)
  _definition (:choice directive named_matcher)
  block (:seq
         "{"
         (:token-immediate (:pattern "\\r?\\n|\\r"))
         (:field :body (:repeat _definition))
         "}")
  single_site (:seq
               (:field :name
                (:seq site_address (:repeat (:seq (:token-immediate (:pattern ", ")) site_address))))
               (:field :body (:repeat _definition)))
  site_block (:seq
              (:field :name
               (:seq site_address (:repeat (:seq (:token-immediate (:pattern ", ")) site_address))))
              block)
  heredoc (:seq
           "<<"
           (:field :identifier heredoc_start)
           (:choice (:field :value (:repeat heredoc_body)) :blank)
           (:field :end_tag heredoc_end))}}
