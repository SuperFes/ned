# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "kdl"
 :word _normal_bare_identifier
 :extras [multi_line_comment]
 :conflicts [[version _ws] [identifier value]]
 :precedences []
 :externals [_eof multi_line_comment _raw_string_start _raw_string_content _raw_string_end]
 :inline []
 :supertypes []
 :rules
 {document (:seq
            (:choice _bom :blank)
            (:choice (:field :version version) :blank)
            (:seq (:repeat _linespace) (:repeat (:seq node (:repeat _linespace)))))
  version (:prec 2
           (:seq
            "/-"
            (:repeat _unicode_space)
            "kdl-version"
            (:repeat1 _unicode_space)
            (:field :version (:choice "1" "2"))
            (:repeat _unicode_space)
            _newline))
  node (:seq _base_node _node_terminator)
  _final_node (:seq (:alias _base_node node) "}")
  _base_node (:seq
              (:choice node_comment :blank)
              (:choice (:seq (:field :type type) (:repeat _node_space)) :blank)
              (:field :name identifier)
              (:repeat node_field)
              (:seq
               (:repeat (:seq (:repeat _node_space) (:field :children node_children_comment)))
               (:choice
                (:seq
                 (:seq (:repeat _node_space) (:field :children node_children))
                 (:repeat (:seq (:repeat _node_space) (:field :children node_children_comment))))
                :blank))
              (:repeat _node_space))
  node_field (:choice
              (:seq (:repeat1 _node_space) _node_field)
              (:seq (:repeat _node_space) node_field_comment))
  node_comment (:seq "/-" (:repeat _linespace))
  node_field_comment (:seq "/-" (:repeat _linespace) _node_field)
  _node_field (:choice (:field :property prop) (:field :argument value))
  node_children_comment (:seq "/-" (:repeat _linespace) node_children)
  node_children (:seq "{" (:repeat (:choice _linespace node)) (:choice "}" _final_node))
  _node_space (:choice _ws _escline)
  _node_terminator (:choice single_line_comment _newline ";" _eof)
  identifier (:choice string _bare_identifier)
  _bare_identifier (:choice _normal_bare_identifier _signed_bare_identifier)
  _normal_bare_identifier (:token
                           (:pattern "[^\\u0009\\u0020\\u00A0\\u1680\\u2000-\\u200A\\u202F\\u205F\\u3000\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF\\\\\\/(){};\\[\\]\"=0-9+\\-][^\\u0009\\u0020\\u00A0\\u1680\\u2000-\\u200A\\u202F\\u205F\\u3000\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF\\\\\\/(){};\\[\\]\"=]*"))
  _signed_bare_identifier (:token
                           (:pattern "[+-](?:[^\\u0009\\u0020\\u00A0\\u1680\\u2000-\\u200A\\u202F\\u205F\\u3000\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF\\\\\\/(){};\\[\\]\"=0-9][^\\u0009\\u0020\\u00A0\\u1680\\u2000-\\u200A\\u202F\\u205F\\u3000\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF\\\\\\/(){};\\[\\]\"=]*)?"))
  keyword (:choice boolean (:token (:prec 2 "null")) (:token (:prec 2 "#null")))
  prop (:seq
        (:field :key identifier)
        (:repeat _node_space)
        "="
        (:repeat _node_space)
        (:field :value value))
  value (:seq
         (:choice (:seq (:field :type type) (:repeat _node_space)) :blank)
         (:field :value (:choice string (:alias _bare_identifier string) number keyword)))
  type (:seq "(" (:repeat _node_space) (:field :name identifier) (:repeat _node_space) ")")
  string (:choice _raw_string multi_line_string _escaped_string)
  _raw_string (:seq
               _raw_string_start
               (:choice (:alias _raw_string_content string_fragment) :blank)
               _raw_string_end)
  multi_line_string (:seq
                     _multiline_open
                     (:repeat
                      (:choice
                       (:alias _multiline_fragment string_fragment)
                       escape
                       escaped_whitespace))
                     "\"\"\"")
  _multiline_open (:token
                   (:seq
                    "\"\"\""
                    (:choice "\r\n" (:pattern "[\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029]"))))
  _multiline_fragment (:token-immediate
                       (:pattern "(?:[^\"\\\\\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF]|\"[^\"\\\\\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF]|\"\"[^\"\\\\\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF])+"))
  _escaped_string (:seq
                   "\""
                   (:repeat
                    (:choice (:alias _string_fragment string_fragment) escape escaped_whitespace))
                   "\"")
  _string_fragment (:token-immediate
                    (:pattern "[^\"\\\\\\u0000-\\u0008\\u000E-\\u001F\\u007F\\u200E\\u200F\\u202A-\\u202E\\u2066-\\u2069\\uFEFF]+"))
  escape (:token-immediate
          (:seq
           "\\"
           (:choice
            "\\"
            "\""
            "/"
            "b"
            "f"
            "n"
            "r"
            "t"
            "s"
            (:pattern "u\\{(?:[0-9a-fA-F]{1,3}|(?:[0-9a-cA-Ce-fE-F][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2})|(?:[1-9a-fA-F][0-9a-fA-F]{4}|0(?:[0-9a-cA-Ce-fE-F][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2}))|(?:0(?:[1-9a-fA-F][0-9a-fA-F]{4}|0(?:[0-9a-cA-Ce-fE-F][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2}))|10[0-9a-fA-F]{4}))\\}"))))
  escaped_whitespace (:token-immediate
                      (:pattern "\\\\(?:\\r\\n|[\\u0009\\u0020\\u00A0\\u1680\\u2000-\\u200A\\u202F\\u205F\\u3000\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029])+"))
  number (:choice keyword_number _decimal _hex _octal _binary)
  _decimal (:seq
            _integer
            (:choice (:seq "." (:alias _fraction decimal)) :blank)
            (:choice (:alias _exponent exponent) :blank))
  _integer (:token (:pattern "[+-]?[0-9][0-9_]*"))
  _fraction (:token-immediate (:pattern "[0-9][0-9_]*"))
  _exponent (:token-immediate (:pattern "[eE][+-]?[0-9][0-9_]*"))
  _hex (:token (:pattern "[+-]?0x[0-9a-fA-F][0-9a-fA-F_]*"))
  _octal (:token (:pattern "[+-]?0o[0-7][0-7_]*"))
  _binary (:token (:pattern "[+-]?0b[01][01_]*"))
  keyword_number (:choice
                  (:token (:prec 2 "#inf"))
                  (:token (:prec 2 "#-inf"))
                  (:token (:prec 2 "#nan")))
  boolean (:choice
           (:token (:prec 2 "true"))
           (:token (:prec 2 "false"))
           (:token (:prec 2 "#true"))
           (:token (:prec 2 "#false")))
  _escline (:seq "\\" (:repeat _ws) (:choice single_line_comment _newline _eof))
  _linespace (:choice _ws _escline _newline single_line_comment)
  _newline (:choice
            (:pattern "\\r\\n")
            (:pattern "\\r")
            (:pattern "\\n")
            (:pattern "\\u0085")
            (:pattern "\\u000B")
            (:pattern "\\u000C")
            (:pattern "\\u2028")
            (:pattern "\\u2029"))
  _ws (:choice _unicode_space multi_line_comment)
  _bom (:pattern "\\u{FEFF}")
  _unicode_space (:pattern "[\\u0009\\u0020\\u00A0\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2007\\u2008\\u2009\\u200A\\u202F\\u205F\\u3000]")
  single_line_comment (:seq
                       "//"
                       (:repeat (:pattern "[^\\r\\n\\u0085\\u000B\\u000C\\u2028\\u2029]"))
                       (:choice _newline _eof))}}
