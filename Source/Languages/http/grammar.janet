# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "http"
 :extras []
 :conflicts [[target_url] [_raw_body] [_section_content]]
 :precedences []
 :externals []
 :inline [_target_url_line]
 :supertypes []
 :rules
 {document (:repeat section)
  WORD_CHAR (:pattern "[\\p{L}\\p{N}]" "u")
  PUNCTUATION (:pattern "[^\\n\\r\\p{Z}\\p{L}\\p{N}]" "u")
  WS (:pattern "\\p{Zs}+" "u")
  NL (:token (:choice "\n" "\r" "\r\n" "\0"))
  LINE_TAIL (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))
  _comment_prefix (:choice
                   (:token (:prec 2 (:pattern "#\\s*")))
                   (:token (:prec 2 (:pattern "\\/\\/\\s*"))))
  comment (:seq
           _comment_prefix
           (:choice
            (:seq
             (:token (:prec 2 "@"))
             (:field :name identifier)
             (:choice
              (:seq
               (:choice (:pattern "\\p{Zs}+" "u") "=")
               (:choice (:token (:prec 1 (:pattern "\\p{Zs}+" "u"))) :blank)
               (:field :value value))
              :blank)
             (:token (:choice "\n" "\r" "\r\n" "\0")))
            (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))
  var_comment (:seq
               _comment_prefix
               (:token (:prec 2 "@"))
               (:field :name identifier)
               (:choice
                (:seq
                 (:choice (:pattern "\\p{Zs}+" "u") "=")
                 (:choice (:token (:prec 1 (:pattern "\\p{Zs}+" "u"))) :blank)
                 (:field :value value))
                :blank)
               (:token (:choice "\n" "\r" "\r\n" "\0")))
  request_separator (:seq
                     (:token (:prec 3 (:pattern "###+\\p{Zs}*")))
                     (:choice (:token (:prec 1 (:pattern "\\p{Zs}+" "u"))) :blank)
                     (:choice (:field :value value) :blank)
                     (:token (:choice "\n" "\r" "\r\n" "\0")))
  section (:prec-right 0
           (:choice (:seq request_separator (:choice _section_content :blank)) _section_content))
  _section_content (:choice
                    (:seq _blank_line (:choice _section_content :blank))
                    (:seq comment (:choice _section_content :blank))
                    (:seq variable_declaration (:choice _section_content :blank))
                    (:seq pre_request_script (:choice _section_content :blank))
                    (:seq
                     (:field :request request)
                     (:repeat
                      (:choice
                       (:seq
                        (:alias var_comment comment)
                        (:repeat (:token (:choice "\n" "\r" "\r\n" "\0"))))
                       (:seq res_handler_script (:repeat (:token (:choice "\n" "\r" "\r\n" "\0"))))))))
  method (:pattern "(OPTIONS|GET|HEAD|POST|PUT|DELETE|TRACE|CONNECT|PATCH|LIST|GRAPHQL|WEBSOCKET)")
  http_version (:token (:prec 0 (:pattern "HTTP\\/[\\d\\.]+")))
  _target_url_line (:repeat1
                    (:choice
                     (:pattern "[\\p{L}\\p{N}]" "u")
                     (:pattern "[^\\n\\r\\p{Z}\\p{L}\\p{N}]" "u")
                     variable))
  target_url (:seq
              _target_url_line
              (:repeat
               (:seq
                (:token (:choice "\n" "\r" "\r\n" "\0"))
                (:pattern "\\p{Zs}+" "u")
                _target_url_line)))
  status_code (:pattern "[1-5]\\d{2}")
  status_text (:pattern "(Continue|Switching Protocols|Processing|OK|Created|Accepted|Non-Authoritative Information|No Content|Reset Content|Partial Content|Multi-Status|Already Reported|IM Used|Multiple Choices|Moved Permanently|Found|See Other|Not Modified|Use Proxy|Switch Proxy|Temporary Redirect|Permanent Redirect|Bad Request|Unauthorized|Payment Required|Forbidden|Not Found|Method Not Allowed|Not Acceptable|Proxy Authentication Required|Request Timeout|Conflict|Gone|Length Required|Precondition Failed|Payload Too Large|URI Too Long|Unsupported Media Type|Range Not Satisfiable|Expectation Failed|I'm a teapot|Misdirected Request|Unprocessable Entity|Locked|Failed Dependency|Too Early|Upgrade Required|Precondition Required|Too Many Requests|Request Header Fields Too Large|Unavailable For Legal Reasons|Internal Server Error|Not Implemented|Bad Gateway|Service Unavailable|Gateway Timeout|HTTP Version Not Supported|Variant Also Negotiates|Insufficient Storage|Loop Detected|Not Extended|Network Authentication Required)")
  response (:seq
            http_version
            (:pattern "\\p{Zs}+" "u")
            status_code
            (:pattern "\\p{Zs}+" "u")
            status_text
            (:token (:choice "\n" "\r" "\r\n" "\0")))
  request (:prec-right 0
           (:seq
            (:choice (:seq (:field :method method) (:pattern "\\p{Zs}+" "u")) :blank)
            (:field :url target_url)
            (:choice (:seq (:pattern "\\p{Zs}+" "u") (:field :version http_version)) :blank)
            (:token (:choice "\n" "\r" "\r\n" "\0"))
            (:repeat comment)
            (:choice response :blank)
            (:repeat (:field :header header))
            (:choice
             (:seq
              (:repeat1 _blank_line)
              (:repeat (:alias var_comment comment))
              (:choice
               (:field :body
                (:choice
                 raw_body
                 multipart_form_data
                 xml_body
                 json_body
                 graphql_body
                 _external_body))
               :blank))
             :blank)))
  query_param (:prec-right 0
               (:seq
                (:field :key value)
                (:choice (:seq "=" (:choice (:field :value value) :blank)) :blank)))
  header (:seq
          (:field :name header_entity)
          (:choice (:pattern "\\p{Zs}+" "u") :blank)
          ":"
          (:choice (:token (:prec 1 (:pattern "\\p{Zs}+" "u"))) :blank)
          (:choice (:field :value (:choice value)) :blank)
          (:token (:choice "\n" "\r" "\r\n" "\0")))
  variable (:seq
            (:token (:prec 1 "{{"))
            (:choice (:pattern "\\p{Zs}+" "u") :blank)
            (:field :name identifier)
            (:choice (:pattern "\\p{Zs}+" "u") :blank)
            (:token (:prec 1 "}}")))
  pre_request_script (:seq
                      "<"
                      (:pattern "\\p{Zs}+" "u")
                      (:choice script path)
                      (:token (:choice "\n" "\r" "\r\n" "\0")))
  res_handler_script (:seq
                      (:token (:prec 3 ">"))
                      (:pattern "\\p{Zs}+" "u")
                      (:choice script path)
                      (:token (:choice "\n" "\r" "\r\n" "\0")))
  script (:seq
          (:token (:prec 1 "{%"))
          (:token (:choice "\n" "\r" "\r\n" "\0"))
          (:repeat (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0")))))
          (:token (:prec 1 "%}")))
  variable_declaration (:seq
                        "@"
                        (:field :name identifier)
                        (:choice (:pattern "\\p{Zs}+" "u") :blank)
                        "="
                        (:choice (:token (:prec 1 (:pattern "\\p{Zs}+" "u"))) :blank)
                        (:field :value value)
                        (:token (:choice "\n" "\r" "\r\n" "\0")))
  xml_body (:seq
            (:token (:prec 2 (:pattern "<[^\\s@]")))
            (:repeat1 (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))
  json_body (:seq
             (:token (:prec 2 (:pattern "[{\\[]\\s+")))
             (:repeat1 (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))
  graphql_body (:seq graphql_data (:choice json_body :blank))
  graphql_data (:seq
                (:token
                 (:prec 2
                  (:seq
                   (:choice "query" "mutation")
                   (:pattern "\\p{Zs}+" "u")
                   (:pattern ".*\\{")
                   (:token (:choice "\n" "\r" "\r\n" "\0")))))
                (:repeat1 (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))
  _external_body (:seq external_body (:token (:choice "\n" "\r" "\r\n" "\0")))
  external_body (:seq
                 (:token (:prec 2 "<"))
                 (:choice (:seq "@" (:field :name identifier)) :blank)
                 (:pattern "\\p{Zs}+" "u")
                 (:field :path path))
  multipart_form_data (:prec-right 0
                       (:seq
                        (:token (:prec 2 "--"))
                        (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))
                        (:repeat
                         (:choice
                          _blank_line
                          comment
                          (:seq
                           external_body
                           (:choice
                            (:pattern "\\p{Zs}+" "u")
                            (:token (:choice "\n" "\r" "\r\n" "\0"))))
                          (:token
                           (:prec 1
                            (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))))))
  raw_body _raw_body
  _raw_body (:seq
             (:choice
              (:token
               (:prec 1 (:token (:seq (:pattern ".*") (:token (:choice "\n" "\r" "\r\n" "\0"))))))
              (:seq _comment_prefix _not_comment))
             (:choice _raw_body :blank))
  _not_comment (:token (:seq (:pattern "[^@]*") (:token (:choice "\n" "\r" "\r\n" "\0"))))
  header_entity (:pattern "[\\w\\-]+")
  identifier (:pattern "[A-Za-z_.\\$\\d\\u00A1-\\uFFFF-]+")
  path (:prec-right 0
        (:repeat1
         (:choice
          (:pattern "[\\p{L}\\p{N}]" "u")
          (:pattern "[^\\n\\r\\p{Z}\\p{L}\\p{N}]" "u")
          variable
          (:token (:pattern "\\\\[^\\n\\r]")))))
  value (:repeat1
         (:choice
          (:pattern "[\\p{L}\\p{N}]" "u")
          (:pattern "[^\\n\\r\\p{Z}\\p{L}\\p{N}]" "u")
          variable
          (:pattern "\\p{Zs}+" "u")))
  _blank_line (:seq
               (:choice (:pattern "\\p{Zs}+" "u") :blank)
               (:token (:prec -1 (:token (:choice "\n" "\r" "\r\n" "\0")))))}}
