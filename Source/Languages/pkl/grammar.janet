# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "pkl"
 :word identifier
 :extras [lineComment blockComment shebangComment (:pattern "[ \\t\\f\\r\\n;]")]
 :conflicts [[typedIdentifier unqualifiedAccessExpr] [qualifiedIdentifier]]
 :precedences []
 :externals [_sl_string_chars
             _sl1_string_chars
             _sl2_string_chars
             _sl3_string_chars
             _sl4_string_chars
             _sl5_string_chars
             _sl6_string_chars
             _ml_string_chars
             _ml1_string_chars
             _ml2_string_chars
             _ml3_string_chars
             _ml4_string_chars
             _ml5_string_chars
             _ml6_string_chars
             _open_subscript_bracket
             _open_argument_paren
             _binary_minus]
 :inline []
 :supertypes []
 :reserved
 {:global ["_"
            "abstract"
            "amends"
            "as"
            "class"
            "const"
            "else"
            "extends"
            "external"
            "false"
            "fixed"
            "for"
            "function"
            "hidden"
            "if"
            "import"
            "import*"
            "in"
            "is"
            "let"
            "local"
            "module"
            "new"
            "nothing"
            "null"
            "open"
            "out"
            "outer"
            "read"
            "read*"
            "read?"
            "super"
            "this"
            "throw"
            "trace"
            "true"
            "typealias"
            "unknown"
            "when"]}
 :rules
 {module (:seq
          (:choice shebangComment :blank)
          (:choice moduleHeader :blank)
          (:repeat (:choice importClause importGlobClause))
          (:repeat _moduleMember))
  shebangComment (:seq "#!" (:pattern ".*"))
  moduleHeader (:seq
                (:choice docComment :blank)
                (:repeat annotation)
                (:choice
                 (:seq moduleClause (:choice extendsOrAmendsClause :blank))
                 extendsOrAmendsClause))
  moduleClause (:seq (:repeat modifier) "module" qualifiedIdentifier)
  extendsOrAmendsClause (:seq (:choice "extends" "amends") stringConstant)
  importClause (:seq "import" stringConstant (:choice (:seq "as" identifier) :blank))
  importGlobClause (:seq "import*" stringConstant (:choice (:seq "as" identifier) :blank))
  _moduleMember (:choice clazz typeAlias classProperty classMethod)
  clazz (:seq
         (:choice docComment :blank)
         (:repeat annotation)
         (:repeat modifier)
         "class"
         identifier
         (:choice typeParameterList :blank)
         (:choice classExtendsClause :blank)
         (:choice classBody :blank))
  classExtendsClause (:seq "extends" qualifiedIdentifier (:choice typeArgumentList :blank))
  classBody (:seq "{" (:repeat (:choice classProperty classMethod)) "}")
  typeAlias (:seq
             (:choice docComment :blank)
             (:repeat annotation)
             (:repeat modifier)
             "typealias"
             identifier
             (:choice typeParameterList :blank)
             "="
             _type)
  classProperty (:seq
                 (:choice docComment :blank)
                 (:repeat annotation)
                 (:repeat modifier)
                 identifier
                 (:choice
                  typeAnnotation
                  (:seq
                   (:choice typeAnnotation :blank)
                   (:choice (:seq "=" _expr) (:repeat1 objectBody)))))
  classMethod (:seq
               (:choice docComment :blank)
               (:repeat annotation)
               methodHeader
               (:choice (:seq "=" _expr) :blank))
  methodHeader (:seq
                (:repeat modifier)
                "function"
                identifier
                (:choice typeParameterList :blank)
                parameterList
                (:choice typeAnnotation :blank))
  annotation (:seq "@" qualifiedIdentifier (:choice objectBody :blank))
  objectBody (:seq "{" (:choice objectBodyParameters :blank) (:repeat _objectMember) "}")
  _objectMember (:prec -2
                 (:choice
                  objectProperty
                  objectMethod
                  objectEntry
                  objectElement
                  memberPredicate
                  forGenerator
                  whenGenerator
                  objectSpread))
  objectProperty (:seq
                  (:repeat modifier)
                  identifier
                  (:choice (:seq (:choice typeAnnotation :blank) "=" _expr) (:repeat1 objectBody)))
  objectMethod (:seq methodHeader "=" _expr)
  objectEntry (:seq
               "["
               (:field :key _expr)
               "]"
               (:choice (:seq "=" (:field :valueExpr _expr)) (:repeat1 objectBody)))
  objectElement _expr
  memberPredicate (:seq
                   "[["
                   (:field :conditionExpr _expr)
                   "]]"
                   (:choice (:seq "=" (:field :valueExpr _expr)) (:repeat1 objectBody)))
  forGenerator (:seq
                "for"
                "("
                _parameter
                (:choice (:seq "," _parameter) :blank)
                "in"
                _expr
                ")"
                objectBody)
  whenGenerator (:seq
                 "when"
                 "("
                 (:field :conditionExpr _expr)
                 ")"
                 (:field :thenBody objectBody)
                 (:choice (:seq "else" (:field :elseBody objectBody)) :blank))
  objectSpread (:seq (:choice "..." "...?") _expr)
  objectBodyParameters (:seq
                        (:seq _parameter (:repeat (:seq "," _parameter)) (:choice "," :blank))
                        "->")
  typeAnnotation (:seq ":" _type)
  _type (:choice
         (:alias "unknown" unknownType)
         (:alias "nothing" nothingType)
         (:alias "module" moduleType)
         stringLiteralType
         declaredType
         parenthesizedType
         nullableType
         constrainedType
         unionType
         defaultUnionType
         functionLiteralType)
  stringLiteralType stringConstant
  declaredType (:prec-right 30 (:seq qualifiedIdentifier (:choice typeArgumentList :blank)))
  parenthesizedType (:seq "(" _type ")")
  nullableType (:prec 29 (:seq _type "?"))
  constrainedType (:prec-right 30
                   (:seq
                    _type
                    (:alias _open_argument_paren "(")
                    (:seq _expr (:repeat (:seq "," _expr)) (:choice "," :blank))
                    ")"))
  unionType (:prec-left 28 (:seq _type "|" _type))
  defaultUnionType (:prec-left 29 (:seq "*" _type))
  functionLiteralType (:prec 27
                       (:seq
                        "("
                        (:choice
                         (:seq _type (:repeat (:seq "," _type)) (:choice "," :blank))
                         :blank)
                        ")"
                        "->"
                        _type))
  typeArgumentList (:seq "<" (:seq _type (:repeat (:seq "," _type)) (:choice "," :blank)) ">")
  typeParameterList (:seq
                     "<"
                     (:seq typeParameter (:repeat (:seq "," typeParameter)) (:choice "," :blank))
                     ">")
  typeParameter (:seq (:choice (:choice "in" "out") :blank) identifier)
  parameterList (:seq
                 "("
                 (:choice
                  (:seq _parameter (:repeat (:seq "," _parameter)) (:choice "," :blank))
                  :blank)
                 ")")
  _parameter (:choice typedIdentifier blankIdentifier)
  typedIdentifier (:seq identifier (:choice typeAnnotation :blank))
  blankIdentifier "_"
  argumentList (:seq
                (:alias _open_argument_paren "(")
                (:choice (:seq _expr (:repeat (:seq "," _expr)) (:choice "," :blank)) :blank)
                ")")
  modifier (:choice "external" "abstract" "open" "local" "hidden" "fixed" "const")
  _expr (:choice
         thisExpr
         outerExpr
         moduleExpr
         nullLiteralExpr
         trueLiteralExpr
         falseLiteralExpr
         intLiteralExpr
         floatLiteralExpr
         throwExpr
         traceExpr
         importExpr
         readExpr
         unqualifiedAccessExpr
         slStringLiteralExpr
         mlStringLiteralExpr
         newExpr
         amendExpr
         superAccessExpr
         superSubscriptExpr
         qualifiedAccessExpr
         subscriptExpr
         nonNullExpr
         unaryMinusExpr
         logicalNotExpr
         exponentiationExpr
         multiplicativeExpr
         additiveExpr
         comparisonExpr
         typeTestExpr
         typeCastExpr
         equalityExpr
         logicalAndExpr
         logicalOrExpr
         pipeExpr
         nullCoalesceExpr
         ifExpr
         letExpr
         functionLiteralExpr
         parenthesizedExpr)
  parenthesizedExpr (:seq "(" _expr ")")
  thisExpr "this"
  outerExpr "outer"
  moduleExpr "module"
  nullLiteralExpr "null"
  trueLiteralExpr "true"
  falseLiteralExpr "false"
  intLiteralExpr (:token
                  (:choice
                   (:seq (:pattern "\\d") (:pattern "[\\d_]*"))
                   (:seq "0x" (:pattern "[\\da-fA-F]") (:pattern "[\\da-fA-F_]*"))
                   (:seq "0b" (:pattern "[0-1]") (:pattern "[0-1_]*"))
                   (:seq "0o" (:pattern "[0-7]") (:pattern "[0-7_]*"))))
  floatLiteralExpr (:token
                    (:choice
                     (:seq
                      (:choice (:seq (:pattern "\\d") (:pattern "[\\d_]*")) :blank)
                      "."
                      (:seq (:pattern "\\d") (:pattern "[\\d_]*"))
                      (:choice
                       (:seq
                        (:choice "e" "E")
                        (:choice (:choice "+" "-") :blank)
                        (:seq (:pattern "\\d") (:pattern "[\\d_]*")))
                       :blank))
                     (:seq
                      (:seq (:pattern "\\d") (:pattern "[\\d_]*"))
                      (:seq
                       (:choice "e" "E")
                       (:choice (:choice "+" "-") :blank)
                       (:seq (:pattern "\\d") (:pattern "[\\d_]*"))))))
  stringConstant (:choice
                  (:seq "\"" (:repeat (:choice slStringLiteralPart escapeSequence)) "\"")
                  (:seq
                   "#\""
                   (:repeat
                    (:choice
                     (:alias slStringLiteralPart1 slStringLiteralPart)
                     (:alias escapeSequence1 escapeSequence)))
                   "\"#"))
  slStringLiteralExpr (:choice
                       (:seq
                        "\""
                        (:repeat (:choice slStringLiteralPart escapeSequence stringInterpolation))
                        "\"")
                       (:seq
                        "#\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart1 slStringLiteralPart)
                          (:alias escapeSequence1 escapeSequence)
                          (:alias stringInterpolation1 stringInterpolation)))
                        "\"#")
                       (:seq
                        "##\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart2 slStringLiteralPart)
                          (:alias escapeSequence2 escapeSequence)
                          (:alias stringInterpolation2 stringInterpolation)))
                        "\"##")
                       (:seq
                        "###\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart3 slStringLiteralPart)
                          (:alias escapeSequence3 escapeSequence)
                          (:alias stringInterpolation3 stringInterpolation)))
                        "\"###")
                       (:seq
                        "####\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart4 slStringLiteralPart)
                          (:alias escapeSequence4 escapeSequence)
                          (:alias stringInterpolation4 stringInterpolation)))
                        "\"####")
                       (:seq
                        "#####\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart5 slStringLiteralPart)
                          (:alias escapeSequence5 escapeSequence)
                          (:alias stringInterpolation5 stringInterpolation)))
                        "\"#####")
                       (:seq
                        "######\""
                        (:repeat
                         (:choice
                          (:alias slStringLiteralPart6 slStringLiteralPart)
                          (:alias escapeSequence6 escapeSequence)
                          (:alias stringInterpolation6 stringInterpolation)))
                        "\"######"))
  slStringLiteralPart _sl_string_chars
  slStringLiteralPart1 _sl1_string_chars
  slStringLiteralPart2 _sl2_string_chars
  slStringLiteralPart3 _sl3_string_chars
  slStringLiteralPart4 _sl4_string_chars
  slStringLiteralPart5 _sl5_string_chars
  slStringLiteralPart6 _sl6_string_chars
  mlStringLiteralExpr (:choice
                       (:seq
                        "\"\"\""
                        (:repeat
                         (:choice
                          mlStringLiteralPart
                          mlStringContinuation
                          escapeSequence
                          stringInterpolation))
                        "\"\"\"")
                       (:seq
                        "#\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart1 mlStringLiteralPart)
                          (:alias escapeSequence1 escapeSequence)
                          (:alias mlStringContinuation1 mlStringContinuation)
                          (:alias stringInterpolation1 stringInterpolation)))
                        "\"\"\"#")
                       (:seq
                        "##\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart2 mlStringLiteralPart)
                          (:alias escapeSequence2 escapeSequence)
                          (:alias mlStringContinuation2 mlStringContinuation)
                          (:alias stringInterpolation2 stringInterpolation)))
                        "\"\"\"##")
                       (:seq
                        "###\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart3 mlStringLiteralPart)
                          (:alias escapeSequence3 escapeSequence)
                          (:alias mlStringContinuation3 mlStringContinuation)
                          (:alias stringInterpolation3 stringInterpolation)))
                        "\"\"\"###")
                       (:seq
                        "####\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart4 mlStringLiteralPart)
                          (:alias escapeSequence4 escapeSequence)
                          (:alias mlStringContinuation4 mlStringContinuation)
                          (:alias stringInterpolation4 stringInterpolation)))
                        "\"\"\"####")
                       (:seq
                        "#####\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart5 mlStringLiteralPart)
                          (:alias escapeSequence5 escapeSequence)
                          (:alias mlStringContinuation5 mlStringContinuation)
                          (:alias stringInterpolation5 stringInterpolation)))
                        "\"\"\"#####")
                       (:seq
                        "######\"\"\""
                        (:repeat
                         (:choice
                          (:alias mlStringLiteralPart6 mlStringLiteralPart)
                          (:alias escapeSequence6 escapeSequence)
                          (:alias mlStringContinuation6 mlStringContinuation)
                          (:alias stringInterpolation6 stringInterpolation)))
                        "\"\"\"######"))
  mlStringLiteralPart _ml_string_chars
  mlStringLiteralPart1 _ml1_string_chars
  mlStringLiteralPart2 _ml2_string_chars
  mlStringLiteralPart3 _ml3_string_chars
  mlStringLiteralPart4 _ml4_string_chars
  mlStringLiteralPart5 _ml5_string_chars
  mlStringLiteralPart6 _ml6_string_chars
  escapeSequence (:token-immediate
                  (:seq "\\" (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence1 (:token-immediate
                   (:seq "\\#" (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence2 (:token-immediate
                   (:seq "\\##" (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence3 (:token-immediate
                   (:seq "\\###" (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence4 (:token-immediate
                   (:seq "\\####" (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence5 (:token-immediate
                   (:seq
                    "\\#####"
                    (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  escapeSequence6 (:token-immediate
                   (:seq
                    "\\######"
                    (:choice (:pattern "[tnr\\\\\"]") (:pattern "u\\{[0-9a-fA-F]+}"))))
  mlStringContinuation (:token-immediate (:pattern "\\\\\\r?\\n"))
  mlStringContinuation1 (:token-immediate (:pattern "\\\\#\\r?\\n"))
  mlStringContinuation2 (:token-immediate (:pattern "\\\\##\\r?\\n"))
  mlStringContinuation3 (:token-immediate (:pattern "\\\\###\\r?\\n"))
  mlStringContinuation4 (:token-immediate (:pattern "\\\\####\\r?\\n"))
  mlStringContinuation5 (:token-immediate (:pattern "\\\\#####\\r?\\n"))
  mlStringContinuation6 (:token-immediate (:pattern "\\\\######\\r?\\n"))
  stringInterpolation (:seq (:token-immediate "\\(") _expr ")")
  stringInterpolation1 (:seq (:token-immediate "\\#(") _expr ")")
  stringInterpolation2 (:seq (:token-immediate "\\##(") _expr ")")
  stringInterpolation3 (:seq (:token-immediate "\\###(") _expr ")")
  stringInterpolation4 (:seq (:token-immediate "\\####(") _expr ")")
  stringInterpolation5 (:seq (:token-immediate "\\#####(") _expr ")")
  stringInterpolation6 (:seq (:token-immediate "\\######(") _expr ")")
  newExpr (:seq "new" (:choice _type :blank) objectBody)
  amendExpr (:prec 1
             (:seq (:field :parent (:choice newExpr amendExpr parenthesizedExpr)) objectBody))
  subscriptExpr (:prec-left 22
                 (:seq (:field :receiver _expr) (:alias _open_subscript_bracket "[") _expr "]"))
  unaryMinusExpr (:prec-left 20 (:seq "-" _expr))
  logicalNotExpr (:prec-left 19 (:seq "!" _expr))
  nonNullExpr (:prec-left 21 (:seq _expr "!!"))
  nullCoalesceExpr (:prec-right 8 (:seq _expr (:field :operator "??") _expr))
  exponentiationExpr (:prec-right 18 (:seq _expr (:field :operator "**") _expr))
  multiplicativeExpr (:prec-left 17
                      (:seq _expr (:field :operator (:choice "*" "/" "~/" "%")) _expr))
  additiveExpr (:prec-left 16
                (:seq _expr (:field :operator (:choice "+" (:alias _binary_minus "-"))) _expr))
  comparisonExpr (:prec-left 15 (:seq _expr (:field :operator (:choice "<" "<=" ">=" ">")) _expr))
  equalityExpr (:prec-left 12 (:seq _expr (:field :operator (:choice "==" "!=")) _expr))
  logicalAndExpr (:prec-left 11 (:seq _expr (:field :operator "&&") _expr))
  logicalOrExpr (:prec-left 10 (:seq _expr (:field :operator "||") _expr))
  pipeExpr (:prec-left 9 (:seq _expr (:field :operator "|>") _expr))
  typeTestExpr (:prec 14 (:seq _expr (:field :operator "is") _type))
  typeCastExpr (:prec 14 (:seq _expr (:field :operator "as") _type))
  ifExpr (:prec -4 (:seq "if" "(" _expr ")" _expr "else" _expr))
  letExpr (:prec -5 (:seq "let" "(" _parameter "=" _expr ")" _expr))
  throwExpr (:prec -6 (:seq "throw" "(" _expr ")"))
  traceExpr (:prec -7 (:seq "trace" "(" _expr ")"))
  readExpr (:prec -8 (:seq (:field :variant (:choice "read" "read?" "read*")) "(" _expr ")"))
  importExpr (:seq (:field :variant (:choice "import" "import*")) (:seq "(" stringConstant ")"))
  unqualifiedAccessExpr (:seq identifier (:choice argumentList :blank))
  superAccessExpr (:prec-left 22 (:seq "super" "." identifier (:choice argumentList :blank)))
  superSubscriptExpr (:prec-left 22 (:seq "super" (:alias _open_subscript_bracket "[") _expr "]"))
  qualifiedAccessExpr (:prec-left 22
                       (:seq
                        (:field :receiver _expr)
                        (:choice "." "?.")
                        (:seq identifier (:choice argumentList :blank))))
  functionLiteralExpr (:prec -11 (:seq parameterList "->" _expr))
  qualifiedIdentifier (:seq identifier (:repeat (:seq "." identifier)))
  identifier (:token
              (:choice
               (:seq
                (:pattern "[^\\s0-9:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u00A0]")
                (:repeat
                 (:pattern "[^\\s:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u00A0]")))
               (:pattern "`[^`]*`")))
  lineComment (:choice (:token (:seq (:pattern "\\/\\/[^\\/]") (:pattern ".*"))) "//$")
  docComment (:seq
              (:token (:seq "///" (:pattern ".*")))
              (:repeat (:token (:seq "///" (:pattern ".*")))))
  blockComment (:token (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))}}
