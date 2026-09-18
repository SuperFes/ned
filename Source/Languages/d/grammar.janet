# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "d"
 :word identifier
 :extras [(:pattern "[ \\t\\r\\n\\u2028\\u2029]") comment directive]
 :conflicts [[type_ctor cast_qualifier]
             [storage_class _attribute]
             [parameter_attribute variadic_arguments_attribute]
             [parameter_attribute type]
             [block_statement aggregate_initializer]
             [storage_class linkage_attribute]
             [deprecated_attribute storage_class]
             [type_ctor variadic_arguments_attribute]
             [_attribute storage_class type]
             [foreach_type type]
             [_specified_function_body]
             [_statement_no_case_no_default _specified_function_body]
             [_declaration2 _statement_no_case_no_default]
             [_declaration_or_statement conditional_declaration]
             [storage_class type]
             [type_ctor constructor destructor]
             [label member_initializer]
             [block_statement conditional_declaration]
             [block_statement static_foreach_declaration]
             [_declaration_or_statement static_foreach_declaration]
             [expression_list expression]
             [primary_expression property_expression]
             [parameter template_parameter]
             [_primary_expr constructor postblit]
             [_primary_expr mixin_declaration]
             [_primary_expr destructor]
             [template_instance template_mixin]
             [_qualified_id _primary_expr]
             [_type2 _primary_expr]
             [_qualified_id template_parameter]
             [pragma_declaration block_statement]
             [pragma_declaration _declaration_or_statement]
             [pragma_declaration _declaration2]
             [pragma_declaration pragma_statement _declaration2]
             [alias_reassign _primary_expr]
             [function_literal parameter_attribute]
             [module_declaration storage_class _attribute]
             [function_body _function_contract]
             [storage_class function_literal]
             [storage_class function_literal _attribute]
             [storage_class synchronized_statement _attribute]
             [storage_class _type2]]
 :precedences [["unary"
                "power"
                "multiply"
                "add"
                "shift"
                "compare"
                "bitwise_and"
                "exclusive_or"
                "inclusive_or"
                "logical_or"
                "logical_and"
                "ternary"
                "assignment"]]
 :externals [directive int_literal float_literal _string not_in not_is _after_eof error_sentinel]
 :inline [_identifier_or_template_instance
          _for1
          _for2
          _for3
          _template_value_parameter
          _template_alias_parameter
          _template_sequence_parameter
          _template_type_parameter
          _declarator_identifier_list
          _non_void_initializer
          _parameter
          body
          consequence
          alternative]
 :supertypes []
 :rules
 {source_file (:seq
               (:choice (:choice _bom shebang) :blank)
               (:choice module_def (:repeat _declaration))
               (:choice (:seq end_file _after_eof) :blank))
  _bom (:token-immediate "﻿")
  shebang (:token-immediate (:pattern "#![^\\n]*\\n"))
  escape_sequence (:choice
                   (:token-immediate (:pattern "\\\\['\"?\\\\abfnrtv]"))
                   (:token-immediate (:pattern "\\\\x[0-9A-Fa-f][0-9A-Fa-f]"))
                   (:token-immediate (:pattern "\\\\[0-7]{1,3}"))
                   (:token-immediate (:pattern "\\\\u[0-9A-Fa-f]{4}"))
                   (:token-immediate (:pattern "\\\\U[0-9A-Fa-f]{8}")))
  htmlentity (:token-immediate (:pattern "\\\\&[a-zA-Z_]+;"))
  end_file (:token (:seq (:prec 100 (:choice (:pattern "\\x1a") (:pattern "__EOF__")))))
  comment (:token
           (:choice
            (:seq "//" (:pattern "(\\\\+(.|\\r?\\n)|[^\\\\\\n])*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")
            (:seq
             "/+"
             (:repeat
              (:choice
               (:pattern "[^+]")
               (:pattern "\\++[^+\\/]")
               (:seq
                "/+"
                (:repeat
                 (:choice
                  (:pattern "[^+]")
                  (:pattern "\\++[^+\\/]")
                  (:seq
                   "/+"
                   (:repeat
                    (:choice
                     (:pattern "[^+]")
                     (:pattern "\\++[^+\\/]")
                     (:seq
                      "/+"
                      (:repeat
                       (:choice
                        (:pattern "[^+]")
                        (:pattern "\\++[^+\\/]")
                        (:seq
                         "/+"
                         (:repeat (:choice (:pattern "[^+]") (:pattern "\\++[^+\\/]")))
                         (:pattern "\\++\\/"))))
                      (:pattern "\\++\\/"))))
                   (:pattern "\\++\\/"))))
                (:pattern "\\++\\/"))))
             (:pattern "\\++\\/"))))
  identifier (:pattern "[_\\p{XID_Start}][\\p{XID_Continue}]*")
  token_string (:seq "q{" (:choice _token_string_tokens :blank) "}")
  _token_string_tokens (:repeat1 _token_string_token)
  _token_string_token (:choice
                       (:seq "{" (:choice _token_string_tokens :blank) "}")
                       _token_no_braces)
  _token_no_braces (:choice
                    identifier
                    string_literal
                    char_literal
                    int_literal
                    float_literal
                    keyword
                    "/"
                    "/="
                    "."
                    ".."
                    "..."
                    "&"
                    "&="
                    "&&"
                    "|"
                    "|="
                    "||"
                    "-"
                    "-="
                    "--"
                    "+"
                    "+="
                    "++"
                    "<"
                    "<="
                    "<<"
                    "<<="
                    ">"
                    ">="
                    ">>="
                    ">>>="
                    ">>"
                    ">>>"
                    "!"
                    "!="
                    "("
                    ")"
                    "["
                    "]"
                    "?"
                    ","
                    ";"
                    ":"
                    "$"
                    "="
                    "=="
                    "*"
                    "*="
                    "%"
                    "%="
                    "^"
                    "^="
                    "^^"
                    "^^="
                    "~"
                    "~="
                    "@"
                    "=>"
                    "#"
                    "!in"
                    "!is")
  keyword (:choice
           abstract
           alias
           align
           asm
           assert
           auto
           break
           case
           cast
           catch
           class
           continue
           debug
           default
           delegate
           delete
           deprecated
           do
           else
           enum
           export
           extern
           final
           finally
           for
           foreach
           foreach_reverse
           function
           goto
           if
           import
           in
           interface
           invariant
           is
           lazy
           mixin
           module
           new
           nothrow
           out
           override
           package
           pragma
           private
           protected
           public
           pure
           ref
           return
           scope
           static
           struct
           super
           switch
           synchronized
           template
           this
           throw
           try
           typeid
           typeof
           union
           unittest
           version
           while
           with
           gshared
           traits
           vector
           parameters_
           special_keyword
           (:ref "false")
           (:ref "true")
           null
           _builtin_type
           void
           type_ctor)
  bool (:token "bool")
  byte (:token "byte")
  ubyte (:token "ubyte")
  char (:token "char")
  short (:token "short")
  ushort (:token "ushort")
  int (:token "int")
  uint (:token "uint")
  long (:token "long")
  ulong (:token "ulong")
  cent (:token "cent")
  ucent (:token "ucent")
  wchar (:token "wchar")
  dchar (:token "dchar")
  float (:token "float")
  double (:token "double")
  real (:token "real")
  ifloat (:token "ifloat")
  idouble (:token "idouble")
  ireal (:token "ireal")
  cfloat (:token "cfloat")
  cdouble (:token "cdouble")
  creal (:token "creal")
  size_t (:token "size_t")
  ptrdiff_t (:token "ptrdiff_t")
  string (:token "string")
  cstring (:token "cstring")
  dstring (:token "dstring")
  wstring (:token "wstring")
  noreturn (:token "noreturn")
  (:ref "true") (:token "true")
  (:ref "false") (:token "false")
  null (:token "null")
  dollar "$"
  super (:token "super")
  this (:token "this")
  abstract (:token "abstract")
  alias (:token "alias")
  align (:token "align")
  asm (:token "asm")
  assert (:token "assert")
  auto (:token "auto")
  break (:token "break")
  case (:token "case")
  cast (:token "cast")
  catch (:token "catch")
  class (:token "class")
  const (:token "const")
  continue (:token "continue")
  debug (:token "debug")
  default (:token "default")
  delegate (:token "delegate")
  delete (:token "delete")
  deprecated (:token "deprecated")
  do "do"
  else "else"
  enum "enum"
  export "export"
  extern "extern"
  final (:token "final")
  finally (:token "finally")
  for (:token "for")
  foreach (:token "foreach")
  foreach_reverse (:token "foreach_reverse")
  function (:token "function")
  goto (:token "goto")
  if (:token "if")
  immutable (:token "immutable")
  import (:token "import")
  in (:token "in")
  inout (:token "inout")
  interface (:token "interface")
  invariant (:token "invariant")
  is (:token "is")
  lazy (:token "lazy")
  mixin (:token "mixin")
  module (:token "module")
  new (:token "new")
  nothrow (:token "nothrow")
  out (:token "out")
  override (:token "override")
  package (:token "package")
  pragma (:token "pragma")
  private (:token "private")
  protected (:token "protected")
  public (:token "public")
  pure (:token "pure")
  ref (:token "ref")
  return (:token "return")
  scope (:token "scope")
  shared (:token "shared")
  static (:token "static")
  struct (:token "struct")
  switch (:token "switch")
  synchronized (:token "synchronized")
  template (:token "template")
  throw (:token "throw")
  try (:token "try")
  typeid (:token "typeid")
  typeof (:token "typeof")
  union (:token "union")
  unittest (:token "unittest")
  version (:token "version")
  while (:token "while")
  with (:token "with")
  parameters_ (:token "__parameters")
  gshared (:token "__gshared")
  traits (:token "__traits")
  vector (:token "__vector")
  module_def (:seq module_declaration (:repeat _declaration))
  module_declaration (:seq
                      (:repeat at_attribute)
                      (:choice (:seq deprecated_attribute (:repeat at_attribute)) :blank)
                      module
                      module_fqn
                      ";")
  module_fqn (:seq identifier (:repeat (:seq "." identifier)))
  import_declaration (:seq (:repeat _attribute) import _import_list ";")
  _import_list (:choice
                (:seq imported (:choice (:seq "," _import_list) :blank))
                (:seq imported ":" (:seq import_bind (:repeat (:seq "," import_bind)))))
  imported (:choice module_fqn (:seq (:field :alias identifier) "=" module_fqn))
  import_bind (:seq identifier (:choice (:seq "=" identifier) :blank))
  mixin_declaration (:seq (:repeat _attribute) mixin_expression ";")
  _declaration (:choice
                (:seq _declaration2)
                (:seq (:repeat1 _attribute) "{" (:repeat _declaration) "}"))
  _declaration2 (:prec-left 0
                 (:choice
                  alias_declaration
                  alias_this
                  anonymous_enum_declaration
                  attribute_declaration
                  class_declaration
                  conditional_declaration
                  constructor
                  debug_specification
                  destructor
                  postblit
                  function_declaration
                  enum_declaration
                  import_declaration
                  interface_declaration
                  invariant_declaration
                  mixin_declaration
                  mixin_template_declaration
                  pragma_declaration
                  struct_declaration
                  template_declaration
                  template_mixin
                  union_declaration
                  unittest_declaration
                  variable_declaration
                  manifest_constant
                  version_specification
                  static_assert
                  auto_declaration
                  static_foreach_declaration
                  alias_reassign
                  ";"))
  variable_declaration (:seq
                        (:repeat _attribute)
                        (:repeat storage_class)
                        type
                        (:seq
                         (:choice declarator bitfield_declarator)
                         (:repeat (:seq "," (:choice declarator bitfield_declarator))))
                        ";")
  _declarator_identifier_list (:prec-right 0 (:seq identifier (:repeat (:seq "," identifier))))
  declarator (:prec-right 0
              (:seq
               identifier
               (:choice (:seq (:choice template_parameters :blank) "=" _initializer) :blank)))
  bitfield_declarator (:prec-right 0
                       (:choice
                        (:seq ":" _expr)
                        (:seq identifier ":" _expr (:choice (:seq "=" _initializer) :blank))))
  manifest_constant (:seq
                     (:repeat _attribute)
                     (:repeat storage_class)
                     enum
                     (:repeat storage_class)
                     (:choice type :blank)
                     (:seq manifest_declarator (:repeat (:seq "," manifest_declarator)))
                     ";")
  manifest_declarator (:choice
                       (:seq identifier "=" _initializer)
                       (:seq identifier template_parameters "=" _initializer))
  storage_class (:choice
                 linkage_attribute
                 align_attribute
                 at_attribute
                 type_ctor
                 deprecated
                 static
                 extern
                 abstract
                 final
                 override
                 synchronized
                 auto
                 scope
                 gshared
                 ref
                 _function_attribute_kwd)
  _initializer (:prec-left 0 (:choice _non_void_initializer void))
  _non_void_initializer (:choice _expr aggregate_initializer)
  auto_declaration (:prec-right 0
                    (:seq
                     (:repeat _attribute)
                     (:repeat1 storage_class)
                     (:seq _auto_assignment (:repeat (:seq "," _auto_assignment)))
                     ";"))
  _auto_assignment (:seq
                    (:field :variable identifier)
                    (:choice template_parameters :blank)
                    "="
                    (:field :value _initializer))
  alias_declaration (:seq
                     (:repeat _attribute)
                     (:choice
                      (:seq alias this "=" identifier ";")
                      (:seq
                       alias
                       (:seq alias_initializer (:repeat (:seq "," alias_initializer)))
                       ";")
                      (:seq alias (:repeat storage_class) type _declarator_identifier_list ";")
                      (:seq
                       alias
                       (:repeat storage_class)
                       type
                       identifier
                       (:choice template_parameters :blank)
                       parameters
                       (:repeat member_function_attribute)
                       ";")))
  alias_initializer (:choice
                     (:seq
                      identifier
                      (:choice template_parameters :blank)
                      "="
                      (:repeat storage_class)
                      type)
                     (:seq
                      identifier
                      (:choice template_parameters :blank)
                      "="
                      (:repeat storage_class)
                      function_literal)
                     (:seq
                      identifier
                      (:choice template_parameters :blank)
                      "="
                      (:repeat storage_class)
                      type
                      parameters
                      (:repeat member_function_attribute)))
  alias_assign (:seq identifier "=" type)
  alias_reassign (:seq
                  (:repeat _attribute)
                  (:prec-dynamic -1
                   (:choice
                    (:seq identifier "=" (:repeat storage_class) type ";")
                    (:seq identifier "=" function_literal ";")
                    (:seq
                     identifier
                     "="
                     (:repeat storage_class)
                     type
                     parameters
                     (:repeat member_function_attribute)
                     ";"))))
  type (:prec-right 0 (:seq (:repeat type_ctor) _type2 (:repeat _type_suffix)))
  type_ctor (:choice const immutable inout shared)
  _type2 (:prec-right 0
          (:choice
           void
           _builtin_type
           _qualified_id
           (:seq "." _qualified_id)
           typeof_expression
           (:seq typeof_expression "." _qualified_id)
           (:seq type_ctor "(" type ")")
           vector_type
           traits_expression
           mixin_expression))
  vector_type (:seq vector "(" type ")")
  _builtin_type (:choice
                 bool
                 byte
                 ubyte
                 char
                 short
                 ushort
                 int
                 uint
                 long
                 ulong
                 cent
                 ucent
                 wchar
                 dchar
                 float
                 double
                 real
                 ifloat
                 idouble
                 ireal
                 cfloat
                 cdouble
                 creal
                 size_t
                 ptrdiff_t
                 string
                 cstring
                 dstring
                 wstring
                 noreturn)
  void "void"
  _type_suffix (:prec-right 0
                (:choice
                 "*"
                 (:seq "[" "]")
                 (:seq "[" expression "]")
                 (:seq "[" expression ".." expression "]")
                 (:seq "[" type "]")
                 (:seq delegate parameters (:repeat member_function_attribute))
                 (:seq function parameters (:repeat _function_attribute))))
  _identifier_or_template_instance (:choice identifier template_instance)
  _qualified_id (:prec-right 0
                 (:choice
                  (:seq identifier)
                  (:seq identifier "." _qualified_id)
                  (:seq identifier "[" "]")
                  (:seq identifier "[" expression ".." expression "]")
                  (:seq identifier "[" type "]")
                  (:seq identifier "[" expression "]")
                  (:seq identifier "[" expression "]" "." _qualified_id)
                  (:seq template_instance)
                  (:seq template_instance "." _qualified_id)))
  typeof_expression (:seq typeof "(" (:choice expression return) ")")
  attribute_declaration (:seq (:repeat1 _attribute) ":")
  align_attribute (:prec-right 0 (:seq align (:choice (:seq "(" expression ")") :blank)))
  deprecated_attribute (:prec-right 0 (:seq deprecated (:choice (:seq "(" expression ")") :blank)))
  _attribute (:prec-right 0
              (:choice
               linkage_attribute
               align_attribute
               deprecated_attribute
               pragma_expression
               type_ctor
               private
               package
               (:seq package "(" module_fqn ")")
               protected
               public
               export
               static
               extern
               abstract
               final
               override
               synchronized
               auto
               scope
               gshared
               at_attribute
               _function_attribute_kwd
               ref
               return))
  at_attribute (:prec-right 0
                (:choice
                 (:seq "@" identifier)
                 (:seq "@" identifier arguments)
                 (:seq "@" template_instance)
                 (:seq "@" template_instance arguments)
                 (:seq "@" "(" _argument_list ")")))
  _function_attribute_kwd (:choice nothrow pure)
  linkage_attribute (:prec-right 0
                     (:seq
                      extern
                      "("
                      (:choice
                       "C"
                       "D"
                       "Windows"
                       "System"
                       (:seq "Objective" "-" "C")
                       (:seq "C" "++")
                       (:seq "C" "++" "," (:choice (:alias _argument_list namespace_list) :blank))
                       (:seq "C" "++" "," class)
                       (:seq "C" "++" "," struct))
                      ")"))
  _argument_list (:prec-right 0
                  (:seq expression (:repeat (:seq "," expression)) (:choice "," :blank)))
  arguments (:seq "(" (:choice _argument_list :blank) ")")
  named_argument (:choice expression (:seq identifier ":" expression))
  _named_argument_list (:prec-right 0
                        (:seq
                         named_argument
                         (:repeat (:seq "," named_argument))
                         (:choice "," :blank)))
  named_arguments (:seq "(" (:choice _named_argument_list :blank) ")")
  pragma_declaration (:seq
                      (:repeat _attribute)
                      (:choice
                       (:seq pragma_expression ";")
                       (:seq pragma_expression _declaration)
                       (:seq pragma_expression "{" (:repeat _declaration) "}")))
  pragma_statement (:choice (:seq pragma_expression _statement) (:seq pragma_expression ";"))
  pragma_expression (:choice
                     (:seq pragma "(" identifier ")")
                     (:seq pragma "(" identifier "," _argument_list ")"))
  expression_list (:prec-right 0 (:seq _expr (:repeat (:seq "," _expr))))
  expression _expr
  _expr (:prec-left 0
         (:choice
          assignment_expression
          ternary_expression
          binary_expression
          ternary_expression
          _unary_expr))
  ternary_expression (:prec-right "ternary"
                      (:seq
                       (:field :condition _expr)
                       "?"
                       (:field :consequence expression_list)
                       ":"
                       (:field :alternative _expr)))
  call_expression (:prec-left 0
                   (:choice
                    (:seq _unary_expr named_arguments)
                    (:prec 2 (:seq _builtin_type named_arguments))
                    (:prec 1 (:seq identifier named_arguments))
                    (:seq type named_arguments)))
  primary_expression (:choice
                      (:seq "(" type ")" "." identifier)
                      (:seq "(" type ")" "." template_instance)
                      (:seq _builtin_type "." identifier)
                      (:seq void "." identifier)
                      (:seq "(" expression_list ")")
                      (:seq type_ctor "(" type ")" "." identifier)
                      (:seq vector_type "." identifier))
  _primary_expr (:choice
                 _identifier_or_template_instance
                 (:seq "." _identifier_or_template_instance)
                 primary_expression
                 typeof_expression
                 typeid_expression
                 array_literal
                 is_expression
                 function_literal
                 traits_expression
                 mixin_expression
                 import_expression
                 dollar
                 this
                 super
                 null
                 (:ref "true")
                 (:ref "false")
                 special_keyword
                 int_literal
                 float_literal
                 char_literal
                 string_literal)
  index_expression (:choice
                    (:seq _unary_expr "[" "]")
                    (:seq
                     _unary_expr
                     "["
                     (:seq index (:repeat (:seq "," index)) (:choice "," :blank))
                     "]"))
  index (:seq expression (:choice (:seq ".." expression) :blank))
  assignment_expression (:prec-right -1
                         (:seq
                          _expr
                          (:field :operator
                           (:choice
                            "="
                            "+="
                            "-="
                            "*="
                            "/="
                            "%="
                            "&="
                            "|="
                            "^="
                            "~="
                            "<<="
                            ">>="
                            ">>>="
                            "^^="))
                          _expr))
  binary_expression (:choice
                     logical_or_expression
                     logical_and_expression
                     or_expression
                     xor_expression
                     and_expression
                     equal_expression
                     rel_expression
                     identity_expression
                     add_expression
                     mul_expression
                     shift_expression
                     power_expression)
  logical_or_expression (:prec-left "logical_or" (:seq _expr (:field :operator "||") _expr))
  logical_and_expression (:prec-left "logical_and" (:seq _expr (:field :operator "&&") _expr))
  or_expression (:prec-left "inclusive_or" (:seq _expr (:field :operator "|") _expr))
  xor_expression (:prec-left "exclusive_or" (:seq _expr (:field :operator "^") _expr))
  and_expression (:prec-left "bitwise_and" (:seq _expr (:field :operator "&") _expr))
  equal_expression (:prec-left "compare" (:seq _expr (:field :operator (:choice "==" "!=")) _expr))
  rel_expression (:prec-left "compare"
                  (:seq _expr (:field :operator (:choice "<=" "<" ">" ">=")) _expr))
  identity_expression (:prec-left "compare" (:seq _expr (:choice not_is not_in in is) _expr))
  add_expression (:prec-left "add" (:seq _expr (:field :operator (:choice "+" "-" "~")) _expr))
  mul_expression (:prec-left "multiply" (:seq _expr (:field :operator (:choice "*" "/" "%")) _expr))
  shift_expression (:prec-left "shift"
                    (:seq _expr (:field :operator (:choice "<<" ">>" ">>>")) _expr))
  power_expression (:prec-left "power" (:seq _expr (:field :operator "^^") _unary_expr))
  postfix_expression (:prec-left 0 (:seq _unary_expr (:field :operator (:choice "++" "--"))))
  unary_expression (:prec-right "unary"
                    (:seq
                     (:field :operator (:choice "~" "+" "-" "!" "*" "&" "++" "--"))
                     _unary_expr))
  _unary_expr (:choice
               _primary_expr
               unary_expression
               new_expression
               delete_expression
               assert_expression
               cast_expression
               throw_expression
               call_expression
               index_expression
               postfix_expression
               property_expression)
  property_expression (:choice
                       (:seq "(" type ")" "." _identifier_or_template_instance)
                       (:prec-left 0 (:seq _unary_expr "." _identifier_or_template_instance))
                       (:prec-left 0 (:seq _unary_expr "." new_expression)))
  cast_expression (:prec-right 0
                   (:choice
                    (:seq cast "(" ")" (:field :operand _unary_expr))
                    (:seq cast "(" type ")" (:field :operand _unary_expr))
                    (:seq cast "(" cast_qualifier ")" _unary_expr)))
  cast_qualifier (:choice
                  const
                  (:seq const shared)
                  immutable
                  inout
                  (:seq inout shared)
                  shared
                  (:seq shared const)
                  (:seq shared inout))
  delete_expression (:prec-left 0 (:seq delete _unary_expr))
  throw_expression (:prec-left 0 (:seq throw _unary_expr))
  assert_expression (:seq assert "(" assert_arguments ")")
  assert_arguments (:seq
                    expression
                    (:choice (:seq "," (:seq expression (:repeat (:seq "," expression)))) :blank)
                    (:choice "," :blank))
  mixin_expression (:seq mixin "(" _argument_list ")")
  import_expression (:seq import "(" expression ")")
  new_expression (:prec-left 0
                  (:choice
                   (:seq new type)
                   (:seq new type "[" expression "]")
                   (:seq new type arguments)
                   (:seq
                    new
                    class
                    (:choice arguments :blank)
                    (:choice _base_class_list :blank)
                    aggregate_body)))
  typeid_expression (:seq typeid "(" (:choice type expression) ")")
  is_expression (:prec-right 0
                 (:seq
                  is
                  "("
                  type
                  (:choice identifier :blank)
                  (:choice
                   (:seq
                    (:choice "==" ":")
                    type_specialization
                    (:choice (:seq "," _template_parameter_list) :blank))
                   :blank)
                  ")"))
  type_specialization (:choice
                       type
                       struct
                       union
                       class
                       interface
                       enum
                       vector
                       function
                       delegate
                       super
                       const
                       immutable
                       inout
                       shared
                       return
                       parameters_
                       module
                       package)
  raw_string (:choice
              (:seq
               "`"
               (:token-immediate (:prec 1 (:pattern "[^`]*")))
               (:token-immediate (:pattern "`[cdw]?")))
              (:seq
               "r\""
               (:token-immediate (:prec 1 (:pattern "[^\"]*")))
               (:token-immediate (:pattern "\"[cdw]?"))))
  hex_string (:seq
              "x\""
              (:token-immediate (:prec 1 (:pattern "[0-9A-Fa-f\\s]*")))
              (:token-immediate (:pattern "\"[cdw]?")))
  quoted_string (:seq
                 "\""
                 (:repeat
                  (:choice
                   (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
                   escape_sequence
                   htmlentity))
                 (:token-immediate (:pattern "\"[cdw]?")))
  interpolation_expression (:seq "$(" expression ")")
  interpolated_raw_string (:seq
                           "i`"
                           (:repeat
                            (:choice
                             (:pattern "[^`$]+")
                             (:pattern "\\$[^(`]")
                             interpolation_expression))
                           (:choice "`" "$`"))
  interpolated_escape "\\$"
  interpolated_quoted_string (:seq
                              "i\""
                              (:repeat
                               (:choice
                                (:pattern "[^\"$\\\\]+")
                                (:pattern "\\$[^(]")
                                escape_sequence
                                htmlentity
                                interpolated_escape
                                interpolation_expression))
                              (:choice "\"" "$\""))
  interpolated_token_string (:seq "iq{" (:choice _i_token_string_tokens :blank) "}")
  _i_token_string_tokens (:repeat1 (:choice _token_string_token interpolation_expression))
  _i_token_string_token (:choice
                         (:seq "{" (:choice _i_token_string_tokens :blank) "}")
                         (:choice _token_no_braces interpolation_expression))
  string_literal (:choice
                  _string
                  raw_string
                  hex_string
                  quoted_string
                  token_string
                  interpolated_raw_string
                  interpolated_quoted_string
                  interpolated_token_string)
  char_literal (:choice (:pattern "'[^\\\\']'") (:seq "'" (:choice escape_sequence htmlentity) "'"))
  array_literal (:seq
                 "["
                 (:choice
                  (:seq
                   _array_member_init
                   (:repeat (:seq "," _array_member_init))
                   (:choice "," :blank))
                  :blank)
                 "]")
  _array_member_init (:choice
                      (:seq
                       (:choice (:seq (:field :key expression) ":") :blank)
                       (:field :value _non_void_initializer)))
  function_literal (:prec-right 0
                    (:choice
                     (:seq
                      function
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      (:choice type :blank)
                      (:choice _parameter_with_attributes :blank)
                      _specified_function_body)
                     (:seq
                      function
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      (:choice type :blank)
                      _parameter_with_attributes
                      "=>"
                      _expr)
                     (:seq
                      delegate
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      (:choice type :blank)
                      (:choice _parameter_with_member_attributes :blank)
                      _specified_function_body)
                     (:seq
                      delegate
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      (:choice type :blank)
                      _parameter_with_member_attributes
                      "=>"
                      _expr)
                     (:seq
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      _parameter_with_member_attributes
                      _specified_function_body)
                     (:seq
                      (:choice (:seq (:choice auto :blank) ref) :blank)
                      _parameter_with_member_attributes
                      "=>"
                      _expr)
                     _specified_function_body
                     (:seq identifier "=>" _expr)))
  _parameter_with_attributes (:seq parameters (:repeat _function_attribute))
  _parameter_with_member_attributes (:seq _parameters (:repeat member_function_attribute))
  special_keyword (:choice
                   "__DATE__"
                   "__FILE__"
                   "__FILE_FULL_PATH__"
                   "__FUNCTION__"
                   "__LINE__"
                   "__MODULE__"
                   "__PRETTY_FUNCTION__"
                   "__TIME__"
                   "__TIMESTAMP__"
                   "__VENDOR__"
                   "__VERSION__")
  _statement (:choice _statement_no_case_no_default case_statement)
  _declarations_and_statements (:repeat1 _declaration_or_statement)
  _declaration_or_statement (:choice _declaration _statement)
  scope_statement _declaration_or_statement
  body (:field :body scope_statement)
  consequence (:field :consequence scope_statement)
  alternative (:field :alternative scope_statement)
  _statement_no_case_no_default (:choice
                                 labeled_statement
                                 expression_statement
                                 block_statement
                                 if_statement
                                 while_statement
                                 do_statement
                                 for_statement
                                 foreach_statement
                                 switch_statement
                                 final_switch_statement
                                 continue_statement
                                 break_statement
                                 return_statement
                                 goto_statement
                                 with_statement
                                 synchronized_statement
                                 try_statement
                                 scope_guard_statement
                                 pragma_statement
                                 asm_statement
                                 conditional_statement
                                 static_assert
                                 static_foreach_statement
                                 version_specification
                                 debug_specification)
  labeled_statement (:prec-left 0 (:seq label body))
  label (:seq identifier ":")
  block_statement (:seq
                   "{"
                   (:choice _declarations_and_statements :blank)
                   (:choice label :blank)
                   "}")
  expression_statement (:seq expression_list ";")
  if_statement (:prec-right 0
                (:seq if if_condition consequence (:choice (:seq else alternative) :blank)))
  if_condition (:seq
                "("
                (:choice
                 _expr
                 (:seq auto identifier "=" expression)
                 (:seq scope identifier "=" expression)
                 (:seq (:repeat1 type_ctor) identifier "=" expression)
                 (:seq type identifier "=" expression))
                ")")
  while_statement (:seq while if_condition body)
  do_statement (:seq do body while "(" (:field :condition expression) ")")
  for_statement (:seq
                 for
                 "("
                 _for1
                 (:choice _for2 :blank)
                 (:choice (:seq ";" (:choice _for3 :blank)) :blank)
                 ")"
                 body)
  _for1 (:choice (:field :init _declaration2) (:field :init _statement_no_case_no_default) ";")
  _for2 (:field :test expression)
  _for3 (:field :step expression_list)
  _foreach (:choice foreach foreach_reverse)
  foreach_statement (:choice
                     (:seq
                      _foreach
                      "("
                      (:seq foreach_type (:repeat (:seq "," foreach_type)))
                      ";"
                      expression
                      ")"
                      body)
                     (:seq _foreach "(" foreach_type ";" expression ".." expression ")" body))
  foreach_type (:seq
                (:repeat (:choice ref alias enum scope type_ctor))
                (:choice type :blank)
                identifier)
  switch_statement (:seq switch "(" expression ")" body)
  case_statement (:prec-right 0
                  (:choice
                   (:seq
                    case
                    expression_list
                    (:choice "," :blank)
                    ":"
                    (:repeat (:choice _declaration _statement_no_case_no_default)))
                   (:seq
                    case
                    expression
                    ":"
                    ".."
                    case
                    expression
                    ":"
                    (:repeat (:choice _declaration _statement_no_case_no_default)))
                   (:seq default ":" (:repeat (:choice _declaration _statement_no_case_no_default)))))
  final_switch_statement (:seq final switch_statement)
  continue_statement (:seq continue (:choice identifier :blank) ";")
  break_statement (:seq break (:choice identifier :blank) ";")
  return_statement (:seq return (:choice expression :blank) ";")
  goto_statement (:choice
                  (:seq goto identifier ";")
                  (:seq goto default ";")
                  (:seq goto case ";")
                  (:seq goto case expression ";"))
  with_statement (:seq with "(" expression ")" scope_statement)
  synchronized_statement (:prec-left 0
                          (:seq
                           synchronized
                           (:choice (:seq "(" expression ")") :blank)
                           scope_statement))
  try_statement (:prec-right 0
                 (:seq try body (:repeat catch_statement) (:choice finally_statement :blank)))
  catch_statement (:seq catch (:seq "(" type (:choice identifier :blank) ")") body)
  finally_statement (:seq finally body)
  scope_guard_statement (:seq scope "(" (:choice "exit" "success" "failure") ")" body)
  asm_statement (:seq asm (:repeat _function_attribute) "{" (:choice asm_inline :blank) "}")
  asm_inline (:repeat1 _token_no_braces)
  mixin_statement (:seq mixin "(" _argument_list ")" ";")
  struct_declaration (:seq
                      (:repeat _attribute)
                      (:choice
                       (:seq struct aggregate_body)
                       (:seq struct identifier ";")
                       (:seq struct identifier aggregate_body)
                       (:seq struct identifier template_parameters (:choice constraint :blank) ";")
                       (:seq
                        struct
                        identifier
                        template_parameters
                        (:choice constraint :blank)
                        aggregate_body)))
  union_declaration (:seq
                     (:repeat _attribute)
                     (:choice
                      (:seq union aggregate_body)
                      (:seq union identifier ";")
                      (:seq union identifier aggregate_body)
                      (:seq union identifier template_parameters (:choice constraint :blank) ";")
                      (:seq
                       union
                       identifier
                       template_parameters
                       (:choice constraint :blank)
                       aggregate_body)))
  aggregate_body (:seq "{" (:repeat _declaration) "}")
  aggregate_initializer (:seq
                         "{"
                         (:choice
                          (:seq
                           member_initializer
                           (:repeat (:seq "," member_initializer))
                           (:choice "," :blank))
                          :blank)
                         "}")
  member_initializer (:seq (:choice (:seq identifier ":") :blank) _initializer)
  postblit (:seq
            (:repeat _attribute)
            this
            "("
            this
            ")"
            (:repeat member_function_attribute)
            function_body)
  invariant_declaration (:seq
                         (:repeat _attribute)
                         (:choice
                          (:seq invariant "(" ")" block_statement)
                          (:seq invariant block_statement)
                          (:seq invariant "(" assert_arguments ")" ";")))
  class_declaration (:seq
                     (:repeat _attribute)
                     (:choice
                      (:seq class identifier (:choice template_parameters :blank) ";")
                      (:seq class identifier aggregate_body)
                      (:seq class identifier ":" _base_class_list aggregate_body)
                      (:seq
                       class
                       identifier
                       template_parameters
                       (:choice constraint :blank)
                       aggregate_body)
                      (:seq
                       class
                       identifier
                       template_parameters
                       (:choice constraint :blank)
                       ":"
                       _base_class_list
                       (:choice constraint :blank)
                       aggregate_body)
                      (:seq
                       class
                       identifier
                       template_parameters
                       ":"
                       _base_class_list
                       constraint
                       aggregate_body)))
  _base_class_list (:seq base_class (:repeat (:seq "," base_class)))
  base_class _type2
  constructor (:seq
               (:repeat _attribute)
               (:choice
                (:seq this parameters (:repeat member_function_attribute) function_body)
                (:seq
                 this
                 template_parameters
                 parameters
                 (:repeat member_function_attribute)
                 (:choice constraint :blank)
                 function_body)
                (:seq
                 (:choice shared :blank)
                 static
                 this
                 "("
                 ")"
                 (:repeat member_function_attribute)
                 function_body)))
  destructor (:seq
              (:repeat _attribute)
              (:choice (:seq (:choice shared :blank) static) :blank)
              "~"
              this
              "("
              ")"
              (:repeat member_function_attribute)
              function_body)
  alias_this (:seq (:repeat _attribute) alias identifier this ";")
  interface_declaration (:seq
                         (:repeat _attribute)
                         (:choice
                          (:seq interface identifier ";")
                          (:seq interface identifier aggregate_body)
                          (:seq interface identifier ":" _base_class_list aggregate_body)
                          (:seq interface identifier template_parameters aggregate_body)
                          (:seq
                           interface
                           identifier
                           template_parameters
                           ":"
                           _base_class_list
                           aggregate_body)
                          (:seq
                           interface
                           identifier
                           template_parameters
                           ":"
                           _base_class_list
                           constraint
                           aggregate_body)
                          (:seq interface identifier template_parameters constraint aggregate_body)
                          (:seq
                           interface
                           identifier
                           template_parameters
                           constraint
                           ":"
                           _base_class_list
                           aggregate_body)))
  enum_declaration (:seq
                    (:repeat _attribute)
                    (:choice
                     (:seq enum identifier ";")
                     (:seq enum identifier _enum_body)
                     (:seq enum identifier ":" type ";")
                     (:seq enum identifier ":" type _enum_body)))
  _enum_body (:seq "{" (:seq enum_member (:repeat (:seq "," enum_member)) (:choice "," :blank)) "}")
  _enum_member_attribute (:choice deprecated_attribute at_attribute)
  enum_member (:seq (:repeat _enum_member_attribute) identifier (:choice (:seq "=" _expr) :blank))
  anonymous_enum_declaration (:seq
                              (:repeat _attribute)
                              enum
                              (:choice (:seq ":" type) :blank)
                              "{"
                              (:seq
                               (:choice anonymous_enum_member enum_member)
                               (:repeat (:seq "," (:choice anonymous_enum_member enum_member)))
                               (:choice "," :blank))
                              "}")
  anonymous_enum_member (:seq type identifier "=" _expr)
  function_declaration (:seq
                        (:repeat _attribute)
                        (:prec-right 0
                         (:choice
                          (:seq
                           type
                           identifier
                           parameters
                           (:repeat member_function_attribute)
                           function_body)
                          (:seq
                           type
                           identifier
                           template_parameters
                           parameters
                           (:repeat member_function_attribute)
                           (:choice constraint :blank)
                           function_body)
                          (:seq
                           (:repeat1 storage_class)
                           identifier
                           parameters
                           (:repeat member_function_attribute)
                           function_body)
                          (:seq
                           (:repeat1 storage_class)
                           identifier
                           template_parameters
                           parameters
                           (:repeat member_function_attribute)
                           (:choice constraint :blank)
                           function_body))))
  parameters (:prec-right 0
              (:choice
               (:seq "(" ")")
               (:seq "(" (:seq parameter (:repeat (:seq "," parameter)) (:choice "," :blank)) ")")
               (:seq "(" (:seq parameter (:repeat (:seq "," parameter))) "," ellipses ")")
               (:seq
                "("
                (:seq parameter (:repeat (:seq "," parameter)))
                ","
                _variadic_arguments_attributes
                ellipses
                ")")
               (:seq "(" ellipses ")")
               (:seq "(" _variadic_arguments_attributes ellipses ")")))
  _parameters (:prec-right 0
               (:choice
                (:seq "(" ")")
                (:seq
                 "("
                 (:seq _parameter (:repeat (:seq "," _parameter)) (:choice "," :blank))
                 ")")
                (:seq "(" (:seq _parameter (:repeat (:seq "," _parameter))) "," ellipses ")")
                (:seq
                 "("
                 (:seq _parameter (:repeat (:seq "," _parameter)))
                 ","
                 _variadic_arguments_attributes
                 ellipses
                 ")")
                (:seq "(" ellipses ")")
                (:seq "(" _variadic_arguments_attributes ellipses ")")))
  parameter (:prec-right 0 _parameter)
  _parameter (:prec-right 0
              (:choice
               (:seq (:repeat parameter_attribute) type)
               (:seq (:repeat parameter_attribute) type ellipses)
               (:seq (:repeat parameter_attribute) type "=" _expr (:choice ellipses :blank))
               (:seq (:repeat parameter_attribute) type identifier)
               (:seq (:repeat parameter_attribute) type identifier ellipses)
               (:seq
                (:repeat parameter_attribute)
                type
                identifier
                "="
                _expr
                (:choice ellipses :blank))))
  parameter_attribute (:choice at_attribute type_ctor final in lazy out ref scope auto return)
  ellipses "..."
  _variadic_arguments_attributes (:repeat1 variadic_arguments_attribute)
  variadic_arguments_attribute (:choice const immutable return scope shared)
  _function_attribute (:choice _function_attribute_kwd at_attribute)
  member_function_attribute (:choice const immutable inout return scope shared _function_attribute)
  function_body (:choice
                 (:seq (:choice _in_out_contract_expressions :blank) "=>" _expr ";")
                 (:seq (:repeat _function_contract) (:choice do :blank) block_statement)
                 (:seq (:repeat _function_contract) ";")
                 (:seq (:repeat _function_contract) _in_out_statement))
  _specified_function_body (:seq (:repeat _function_contract) (:choice do :blank) block_statement)
  _function_contract (:choice _in_out_contract_expression _in_out_statement)
  _in_out_contract_expressions (:repeat1 _in_out_contract_expression)
  _in_out_contract_expression (:choice in_contract_expression out_contract_expression)
  _in_out_statement (:choice in_statement out_statement)
  in_contract_expression (:seq in "(" assert_arguments ")")
  out_contract_expression (:seq out "(" (:choice identifier :blank) ";" assert_arguments ")")
  in_statement (:seq in block_statement)
  out_statement (:seq out (:choice (:seq "(" identifier ")") :blank) block_statement)
  template_declaration (:seq
                        (:repeat _attribute)
                        template
                        identifier
                        template_parameters
                        (:choice constraint :blank)
                        "{"
                        (:repeat _declaration)
                        "}")
  template_instance (:prec-left 0 (:seq identifier template_arguments))
  template_arguments (:prec-right 0
                      (:seq
                       "!"
                       (:choice
                        (:seq "(" (:choice _template_argument_list :blank) ")")
                        _template_single_arg)))
  template_argument (:choice type _expr)
  _template_argument_list (:seq
                           template_argument
                           (:repeat (:seq "," template_argument))
                           (:choice "," :blank))
  _template_single_arg (:choice
                        identifier
                        void
                        _builtin_type
                        char_literal
                        string_literal
                        int_literal
                        float_literal
                        (:ref "true")
                        (:ref "false")
                        null
                        this
                        special_keyword)
  template_parameter (:prec-right 0
                      (:choice
                       _template_type_parameter
                       _template_value_parameter
                       _template_alias_parameter
                       _template_sequence_parameter))
  template_parameters (:seq "(" (:choice _template_parameter_list :blank) ")")
  _template_parameter_list (:prec-right 0
                            (:seq
                             template_parameter
                             (:repeat (:seq "," template_parameter))
                             (:choice "," :blank)))
  _template_type_parameter (:prec-right 0
                            (:seq
                             (:choice this :blank)
                             identifier
                             (:choice (:seq ":" type) :blank)
                             (:choice (:seq "=" type) :blank)))
  _template_value_parameter (:prec-right 0
                             (:seq
                              type
                              identifier
                              (:choice (:seq ":" _expr) :blank)
                              (:choice (:seq "=" _expr) :blank)))
  _template_sequence_parameter (:seq identifier "...")
  _template_alias_parameter (:prec-right 0
                             (:seq
                              alias
                              (:choice type :blank)
                              identifier
                              (:choice (:seq ":" (:choice type _expr)) :blank)
                              (:choice (:seq "=" (:choice type _expr)) :blank)))
  constraint (:seq if "(" expression ")")
  mixin_template_declaration (:seq (:repeat _attribute) mixin template_declaration)
  template_mixin (:seq
                  (:repeat _attribute)
                  mixin
                  (:choice (:seq (:choice typeof_expression :blank) ".") :blank)
                  (:seq
                   _identifier_or_template_instance
                   (:repeat (:seq "." _identifier_or_template_instance)))
                  (:choice template_arguments :blank)
                  (:choice identifier :blank)
                  ";")
  conditional_declaration (:seq
                           (:repeat _attribute)
                           (:prec-right 0
                            (:choice
                             (:seq condition _declaration)
                             (:seq condition _declaration else ":" (:repeat _declaration))
                             (:seq condition _declaration else _declaration)
                             (:seq condition _declaration else "{" (:repeat _declaration) "}")
                             (:seq condition "{" (:repeat _declaration) "}")
                             (:seq
                              condition
                              "{"
                              (:repeat _declaration)
                              "}"
                              else
                              ":"
                              (:repeat _declaration))
                             (:seq condition "{" (:repeat _declaration) "}" else _declaration)
                             (:seq
                              condition
                              "{"
                              (:repeat _declaration)
                              "}"
                              else
                              "{"
                              (:repeat _declaration)
                              "}")
                             (:seq condition ":" (:repeat1 _declaration)))))
  conditional_statement (:prec-right 0
                         (:seq condition consequence (:choice (:seq else alternative) :blank)))
  condition (:choice version_condition debug_condition static_if_condition)
  version_condition (:prec-left 0
                     (:seq version "(" (:choice int_literal identifier unittest assert) ")"))
  version_specification (:seq version "=" (:choice int_literal identifier) ";")
  debug_condition (:prec-right 0
                   (:seq debug (:choice (:seq "(" (:choice int_literal identifier) ")") :blank)))
  debug_specification (:seq debug "=" (:choice int_literal identifier) ";")
  static_if_condition (:seq static if "(" expression ")")
  static_foreach_statement (:seq static foreach_statement)
  static_foreach_declaration (:choice
                              (:seq
                               static
                               _foreach
                               "("
                               (:seq foreach_type (:repeat (:seq "," foreach_type)))
                               ";"
                               expression
                               ")"
                               "{"
                               (:repeat _declaration)
                               "}")
                              (:seq
                               static
                               _foreach
                               "("
                               (:seq foreach_type (:repeat (:seq "," foreach_type)))
                               ";"
                               expression
                               ")"
                               _declaration)
                              (:seq
                               static
                               _foreach
                               "("
                               foreach_type
                               ";"
                               expression
                               ".."
                               expression
                               ")"
                               "{"
                               (:repeat _declaration)
                               "}")
                              (:seq
                               static
                               _foreach
                               "("
                               foreach_type
                               ";"
                               expression
                               ".."
                               expression
                               ")"
                               _declaration))
  static_assert (:seq static assert_expression ";")
  traits_expression (:seq
                     traits
                     "("
                     identifier
                     (:choice (:seq "," _template_argument_list) :blank)
                     ")")
  unittest_declaration (:seq (:repeat _attribute) unittest block_statement)}}
