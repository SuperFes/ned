# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "thrift"
 :word identifier
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals []
 :inline [_type_identifier list_separator]
 :supertypes [definition header]
 :rules
 {document (:seq (:repeat header) (:repeat definition))
  header (:choice include_statement namespace_declaration package_declaration)
  include_statement (:seq (:choice "include" "cpp_include") string)
  namespace_declaration (:seq
                         "namespace"
                         namespace_scope
                         (:choice (:alias _type_identifier namespace) string)
                         (:choice namespace_uri :blank)
                         (:choice ";" :blank))
  package_declaration (:seq (:repeat fb_annotation_definition) "package" string)
  namespace_scope (:choice
                   "*"
                   "as3"
                   "c_glib"
                   "cl"
                   "cocoa"
                   "cpp"
                   "cpp2"
                   "cpp.noexist"
                   "csharp"
                   "d"
                   "dart"
                   "delphi"
                   "erl"
                   "go"
                   "haxe"
                   "java"
                   "javame"
                   "js"
                   "kotlin"
                   "lua"
                   "netcore"
                   "netstd"
                   "nodejs"
                   "nodets"
                   "noexist"
                   "ocaml"
                   "perl"
                   "php"
                   "php.path"
                   "py"
                   "py.twisted"
                   "rb"
                   "rs"
                   "smalltalk.prefix"
                   "smalltalk.category"
                   "st"
                   "swift"
                   "ts"
                   "xml"
                   "xsd"
                   "cocoa_prefix"
                   "cpp_namespace"
                   "csharp_namespace"
                   "delphi_namespace"
                   "java_package"
                   "perl_package"
                   "php_namespace"
                   "py_module"
                   "ruby_namespace"
                   "smalltalk_category"
                   "smalltalk_prefix"
                   "xsd_namespace"
                   "android"
                   "hack"
                   "hack.module"
                   "hs"
                   "hs2"
                   "java.swift"
                   "java.swift.constants"
                   "json"
                   "py.asyncio"
                   "py3"
                   "rust"
                   "scala")
  namespace_uri (:seq "(" "uri" "=" string ")")
  definition (:choice
              const_definition
              typedef_definition
              enum_definition
              senum_definition
              struct_definition
              union_definition
              exception_definition
              service_definition
              interaction_definition)
  const_definition (:seq
                    (:repeat fb_annotation_definition)
                    "const"
                    type
                    identifier
                    "="
                    literal
                    (:choice annotation_definition :blank)
                    (:choice list_separator :blank))
  typedef_definition (:seq
                      (:repeat fb_annotation_definition)
                      "typedef"
                      definition_type
                      (:choice annotation_definition :blank)
                      (:alias identifier typedef_identifier)
                      (:choice annotation_definition :blank)
                      (:choice list_separator :blank))
  enum_definition (:seq
                   (:repeat fb_annotation_definition)
                   "enum"
                   _type_identifier
                   "{"
                   (:repeat
                    (:seq
                     (:repeat fb_annotation_definition)
                     _identifier_with_dots
                     (:choice (:seq "=" number) :blank)
                     (:choice annotation_definition :blank)
                     (:choice list_separator :blank)))
                   "}"
                   (:choice annotation_definition :blank))
  senum_definition (:seq
                    "senum"
                    _type_identifier
                    "{"
                    (:repeat (:seq string (:choice list_separator :blank)))
                    "}")
  struct_definition (:seq
                     (:repeat fb_annotation_definition)
                     "struct"
                     _type_identifier
                     (:choice "xsd_all" :blank)
                     "{"
                     (:repeat field)
                     "}"
                     (:choice annotation_definition :blank))
  union_definition (:seq
                    (:repeat fb_annotation_definition)
                    "union"
                    _type_identifier
                    (:choice "xsd_all" :blank)
                    "{"
                    (:repeat field)
                    "}"
                    (:choice annotation_definition :blank))
  exception_definition (:seq
                        (:repeat fb_annotation_definition)
                        (:repeat exception_modifier)
                        "exception"
                        identifier
                        "{"
                        (:repeat field)
                        "}"
                        (:choice annotation_definition :blank))
  exception_modifier (:choice "client" "permanent" "server" "safe" "stateful" "transient")
  service_definition (:seq
                      (:repeat fb_annotation_definition)
                      "service"
                      _type_identifier
                      (:choice (:seq "extends" _type_identifier) :blank)
                      "{"
                      (:repeat (:choice (:seq "performs" _type_identifier ";") function_definition))
                      "}"
                      (:choice annotation_definition :blank))
  interaction_definition (:seq
                          "interaction"
                          _type_identifier
                          "{"
                          (:repeat function_definition)
                          "}"
                          (:choice annotation_definition :blank))
  field (:seq
         (:repeat fb_annotation_definition)
         (:choice field_id :blank)
         (:choice field_modifier :blank)
         type
         (:choice "&" :blank)
         (:choice annotation_definition :blank)
         _identifier_with_dots
         (:choice (:seq "=" literal) :blank)
         (:choice "xsd_optional" :blank)
         (:choice "xsd_nillable" :blank)
         (:choice xsd_attrs :blank)
         (:choice annotation_definition :blank)
         (:choice list_separator :blank))
  field_id (:seq number ":")
  field_modifier (:choice "required" "optional")
  xsd_attrs (:seq "xsd_attrs" "{" (:repeat field) "}")
  function_definition (:seq
                       (:repeat fb_annotation_definition)
                       (:choice function_modifier :blank)
                       (:seq type (:repeat (:seq "," type)))
                       (:choice annotation_definition :blank)
                       identifier
                       parameters
                       (:choice throws :blank)
                       (:choice annotation_definition :blank)
                       (:choice list_separator :blank))
  function_modifier (:choice "async" "oneway" "readonly" "idempotent")
  parameters (:seq "(" (:repeat parameter) ")")
  parameter (:seq
             (:repeat fb_annotation_definition)
             (:choice field_id :blank)
             (:choice field_modifier :blank)
             type
             (:choice annotation_definition :blank)
             identifier
             (:choice (:seq "=" literal) :blank)
             (:choice "xsd_optional" :blank)
             (:choice "xsd_nillable" :blank)
             (:choice xsd_attrs :blank)
             (:choice annotation_definition :blank)
             (:choice list_separator :blank))
  throws (:seq "throws" parameters)
  type (:choice _type_identifier primitive container_type "void")
  definition_type (:choice primitive container_type _type_identifier)
  primitive (:choice
             "binary"
             "bool"
             "byte"
             "i8"
             "i16"
             "i32"
             "i64"
             "float"
             "double"
             "string"
             "slist")
  container_type (:choice list map set sink stream)
  list (:prec-right 0
        (:seq
         "list"
         "<"
         type
         (:choice annotation_definition :blank)
         ">"
         (:choice annotation_definition :blank)
         (:choice string :blank)))
  map (:prec-right 0
       (:seq
        "map"
        (:choice string :blank)
        "<"
        type
        (:choice annotation_definition :blank)
        ","
        type
        (:choice annotation_definition :blank)
        ">"
        (:choice annotation_definition :blank)))
  set (:prec-right 0
       (:seq
        "set"
        (:choice string :blank)
        "<"
        type
        (:choice annotation_definition :blank)
        ">"
        (:choice annotation_definition :blank)))
  sink (:prec-right 0
        (:seq
         "sink"
         "<"
         type
         (:choice annotation_definition :blank)
         (:choice throws :blank)
         ","
         type
         (:choice annotation_definition :blank)
         (:choice throws :blank)
         ">"))
  stream (:prec-right 0 (:seq "stream" "<" type (:choice throws :blank) ">"))
  annotation_definition (:seq
                         "("
                         (:choice
                          (:seq
                           (:seq annotation_identifier (:choice (:seq "=" literal) :blank))
                           (:repeat
                            (:seq
                             ","
                             (:seq annotation_identifier (:choice (:seq "=" literal) :blank))))
                           (:choice "," :blank))
                          :blank)
                         ")")
  fb_annotation_definition (:seq
                            "@"
                            annotation_identifier
                            (:choice
                             (:seq
                              "{"
                              (:repeat (:seq identifier "=" literal (:choice "," :blank)))
                              "}")
                             :blank))
  literal (:prec-right 0
           (:choice
            number
            double
            boolean
            string
            list_literal
            map_literal
            struct_literal
            _type_identifier))
  number (:token
          (:choice
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:pattern "0[xX]")
            (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
           (:seq (:choice (:choice "-" "+") :blank) (:pattern "0[bB]") (:pattern "[01](_?[01])*"))
           (:choice
            (:seq
             (:choice (:choice "-" "+") :blank)
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank))))
            (:pattern "\\d(_?\\d)*")
            (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))))
  double (:pattern "[+-]?(\\d+(\\.\\d+)?|\\.\\d+)([Ee][+-]?\\d+)?")
  boolean (:choice "true" "false")
  list_literal (:seq "[" (:repeat (:seq literal (:choice list_separator :blank))) "]")
  map_literal (:seq "{" (:repeat (:seq literal ":" literal (:choice list_separator :blank))) "}")
  struct_literal (:seq
                  _type_identifier
                  "{"
                  (:repeat (:seq identifier "=" literal (:choice "," :blank)))
                  "}")
  string (:choice
          (:seq
           "\""
           (:repeat
            (:choice (:alias unescaped_double_string_fragment string_fragment) _escape_sequence))
           "\"")
          (:seq
           "'"
           (:repeat
            (:choice (:alias unescaped_single_string_fragment string_fragment) _escape_sequence))
           "'"))
  unescaped_double_string_fragment (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
  unescaped_single_string_fragment (:token-immediate (:prec 1 (:pattern "[^'\\\\]+")))
  _escape_sequence (:choice
                    (:prec 2 (:token-immediate (:seq "\\" (:pattern "[^abfnrtvxu'\\\"\\\\\\?]"))))
                    (:prec 1 escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u{[0-9a-fA-F]+}")
                     (:pattern "U[0-9a-fA-F]{8}"))))
  identifier (:pattern "[A-Za-z_][A-Za-z0-9_]*")
  _identifier_with_dots (:alias (:pattern "[A-Za-z_][A-Za-z0-9_.]*") identifier)
  _type_identifier (:seq (:field :type identifier) (:repeat (:seq "." identifier)))
  annotation_identifier _type_identifier
  list_separator (:choice "," ";")
  comment (:token
           (:choice
            (:seq "#" (:pattern "(\\\\(.|\\r?\\n)|[^\\\\\\n])*"))
            (:seq "//" (:pattern "(\\\\(.|\\r?\\n)|[^\\\\\\n])*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
