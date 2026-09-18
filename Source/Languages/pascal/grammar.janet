# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "pascal"
 :word identifier
 :extras [_space comment pp]
 :conflicts [[_declProc]
             [_declOperator]
             [declConst]
             [declVar]
             [declType]
             [declProp]
             [declProcFwd]
             [declVars]
             [declConsts]
             [declTypes]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {root (:choice (:choice program library unit _definitions) :blank)
  program (:seq
           kProgram
           moduleName
           ";"
           (:choice _definitions :blank)
           (:alias blockTr block)
           kEndDot)
  library (:seq
           kLibrary
           moduleName
           ";"
           (:choice _definitions :blank)
           (:choice (:alias blockTr block) kEnd)
           kEndDot)
  unit (:seq
        kUnit
        moduleName
        ";"
        (:repeat (:choice interface implementation initialization finalization))
        kEnd
        kEndDot)
  interface (:seq kInterface (:choice _declarations :blank))
  implementation (:seq kImplementation (:choice _definitions :blank))
  initialization (:seq kInitialization (:choice _statementsTr :blank))
  finalization (:seq kFinalization (:choice _statementsTr :blank))
  moduleName (:seq (:choice (:repeat1 (:prec 0 (:seq identifier kDot))) :blank) identifier)
  if (:seq kIf (:field :condition _expr) kThen (:field :then _statement))
  nestedIf (:prec 1 if)
  ifElse (:prec-right 1
          (:seq
           kIf
           (:field :condition _expr)
           kThen
           (:field :then (:choice (:choice _statementTr if) :blank))
           kElse
           (:field :else _statement)))
  while (:seq kWhile (:field :condition _expr) kDo (:field :body _statement))
  repeat (:prec 2
          (:seq
           kRepeat
           (:field :body (:choice (:alias statementsTr statements) :blank))
           kUntil
           (:field :condition _expr)
           ";"))
  for (:seq
       kFor
       (:field :start assignment)
       (:choice kTo kDownto)
       (:field :end _expr)
       kDo
       (:field :body _statement))
  foreach (:seq
           kFor
           (:field :iterator _expr)
           kIn
           (:field :iterable _expr)
           kDo
           (:field :body _statement))
  exceptionHandler (:seq
                    kOn
                    (:field :variable (:choice (:seq identifier ":") :blank))
                    (:field :exception typeref)
                    kDo
                    (:field :body _statement))
  exceptionElse (:seq kElse (:repeat _statement) _statement)
  _exceptionHandlers (:seq
                      (:repeat exceptionHandler)
                      (:choice exceptionHandler (:alias exceptionHandlerTr exceptionHandler))
                      (:choice exceptionElse :blank))
  try (:prec 2
       (:seq
        kTry
        (:field :try (:choice (:alias statementsTr statements) :blank))
        (:choice
         (:field :except
          (:seq
           kExcept
           (:choice (:choice (:alias statementsTr statements) _exceptionHandlersTr) :blank)))
         (:field :finally (:seq kFinally (:choice (:alias statementsTr statements) :blank))))
        kEnd
        ";"))
  caseCase (:seq (:field :label caseLabel) (:field :body _statement))
  case (:prec 2
        (:seq
         kCase
         _expr
         kOf
         (:repeat caseCase)
         (:choice (:alias caseCaseTr caseCase) :blank)
         (:choice (:seq kElse (:choice ":" :blank) (:choice _statementsTr :blank)) :blank)
         kEnd
         ";"))
  block (:seq kBegin (:choice _statementsTr :blank) kEnd ";")
  asm (:seq kAsm (:choice asmBody :blank) kEnd ";")
  with (:seq
        kWith
        (:seq
         (:choice (:repeat1 (:prec 0 (:seq (:field :entity _expr) ","))) :blank)
         (:field :entity _expr))
        kDo
        (:field :body _statement))
  raise (:seq kRaise (:field :exception _expr) ";")
  statement (:choice (:seq _expr ";"))
  goto (:seq kGoto identifier ";")
  _statement (:choice
              ";"
              (:seq assignment ";")
              (:seq varDef ";")
              (:alias statement statement)
              (:alias if if)
              (:alias ifElse ifElse)
              (:alias while while)
              (:alias repeat repeat)
              (:alias for for)
              (:alias foreach foreach)
              (:alias try try)
              (:alias case case)
              (:alias block block)
              (:alias with with)
              (:alias raise raise)
              (:alias goto goto)
              (:alias asm asm))
  ifTr (:seq kIf (:field :condition _expr) kThen (:field :then (:choice _statementTr :blank)))
  nestedIfTr (:prec 1 if)
  ifElseTr (:prec-right 1
            (:seq
             kIf
             (:field :condition _expr)
             kThen
             (:field :then (:choice (:choice _statementTr if) :blank))
             kElse
             (:field :else (:choice _statementTr :blank))))
  whileTr (:seq kWhile (:field :condition _expr) kDo (:field :body (:choice _statementTr :blank)))
  repeatTr (:prec 2
            (:seq
             kRepeat
             (:field :body (:choice (:alias statementsTr statements) :blank))
             kUntil
             (:field :condition _expr)))
  forTr (:seq
         kFor
         (:field :start assignment)
         (:choice kTo kDownto)
         (:field :end _expr)
         kDo
         (:field :body (:choice _statementTr :blank)))
  foreachTr (:seq
             kFor
             (:field :iterator _expr)
             kIn
             (:field :iterable _expr)
             kDo
             (:field :body (:choice _statementTr :blank)))
  exceptionHandlerTr (:seq
                      kOn
                      (:field :variable (:choice (:seq identifier ":") :blank))
                      (:field :exception typeref)
                      kDo
                      (:field :body (:choice _statementTr :blank)))
  exceptionElseTr (:seq kElse (:repeat _statement) (:choice _statementTr :blank))
  _exceptionHandlersTr (:seq
                        (:repeat exceptionHandler)
                        (:choice exceptionHandler (:alias exceptionHandlerTr exceptionHandler))
                        (:choice exceptionElse :blank))
  tryTr (:prec 2
         (:seq
          kTry
          (:field :try (:choice (:alias statementsTr statements) :blank))
          (:choice
           (:field :except
            (:seq
             kExcept
             (:choice (:choice (:alias statementsTr statements) _exceptionHandlersTr) :blank)))
           (:field :finally (:seq kFinally (:choice (:alias statementsTr statements) :blank))))
          kEnd))
  caseCaseTr (:seq (:field :label caseLabel) (:field :body (:choice _statementTr :blank)))
  caseTr (:prec 2
          (:seq
           kCase
           _expr
           kOf
           (:repeat caseCase)
           (:choice (:alias caseCaseTr caseCase) :blank)
           (:choice (:seq kElse (:choice ":" :blank) (:choice _statementsTr :blank)) :blank)
           kEnd))
  blockTr (:seq kBegin (:choice _statementsTr :blank) kEnd)
  asmTr (:seq kAsm (:choice asmBody :blank) kEnd)
  withTr (:seq
          kWith
          (:seq
           (:choice (:repeat1 (:prec 0 (:seq (:field :entity _expr) ","))) :blank)
           (:field :entity _expr))
          kDo
          (:field :body (:choice _statementTr :blank)))
  raiseTr (:seq kRaise (:field :exception _expr))
  statementTr (:choice (:seq _expr))
  gotoTr (:seq kGoto identifier)
  _statementTr (:choice
                (:seq assignment)
                (:seq varDef)
                (:alias statementTr statement)
                (:alias ifTr if)
                (:alias ifElseTr ifElse)
                (:alias whileTr while)
                (:alias repeatTr repeat)
                (:alias forTr for)
                (:alias foreachTr foreach)
                (:alias tryTr try)
                (:alias caseTr case)
                (:alias blockTr block)
                (:alias withTr with)
                (:alias raiseTr raise)
                (:alias gotoTr goto)
                (:alias asmTr asm))
  assignment (:prec-left 1
              (:seq
               (:field :lhs (:choice _expr varAssignDef))
               (:field :operator (:choice kAssign kAssignAdd kAssignSub kAssignMul kAssignDiv))
               (:field :rhs _expr)))
  varAssignDef (:seq kVar identifier (:choice (:seq ":" (:field :type typeref)) :blank))
  varDef (:seq kVar identifier ":" (:field :type typeref))
  label (:seq identifier ":")
  caseLabel (:seq
             (:seq
              (:choice (:repeat1 (:prec 0 (:seq (:choice _expr range) ","))) :blank)
              (:choice _expr range))
             ":")
  _statements (:repeat1 (:choice varDef _statement label))
  _statementsTr (:seq (:repeat (:choice _statement label)) (:choice _statementTr _statement))
  statements _statements
  statementsTr _statementsTr
  asmBody (:repeat1
           (:choice
            identifier
            (:pattern "[0-9a-fA-F]")
            (:pattern "[.,:;+\\-*\\[\\]<>&%$]")
            (:pattern "\\([^*]|\\)")))
  _expr (:choice _ref exprBinary exprUnary)
  _ref (:choice
        (:prec-left 0
         (:choice (:seq kSpecialize identifier) (:seq (:alias kSpecialize identifier))))
        identifier
        _literal
        inherited
        exprDot
        exprBrackets
        exprParens
        exprSubscript
        exprCall
        (:alias exprDeref exprUnary)
        (:alias exprAs exprBinary)
        exprTpl
        lambda)
  lambda (:seq
          (:choice kProcedure kFunction)
          (:field :args (:choice declArgs :blank))
          (:choice (:seq ":" (:field :type typeref)) :blank)
          (:field :local (:choice _definitions :blank))
          (:field :body (:choice (:alias blockTr block) (:alias asmTr asm))))
  inherited (:prec-right 0 (:seq kInherited (:choice identifier :blank)))
  exprDot (:prec-left 5 (:seq (:field :lhs _ref) (:field :operator kDot) (:field :rhs _ref)))
  exprDeref (:prec-left 4 (:seq (:field :operand _expr) (:field :operator kHat)))
  exprAs (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kAs) (:field :rhs _expr)))
  exprTpl (:prec-left 5
           (:seq
            (:field :entity _ref)
            kLt
            (:field :args (:seq (:choice (:repeat1 (:prec 5 (:seq _expr ","))) :blank) _expr))
            kGt))
  exprSubscript (:prec-left 5 (:seq (:field :entity _ref) "[" (:field :args exprArgs) "]"))
  exprCall (:prec-left 5
            (:seq (:field :entity _ref) "(" (:field :args (:choice exprArgs :blank)) ")"))
  legacyFormat (:repeat1 (:seq ":" _expr))
  exprArgs (:seq
            (:choice
             (:repeat1 (:prec 0 (:seq (:seq _expr (:choice legacyFormat :blank)) ",")))
             :blank)
            (:seq _expr (:choice legacyFormat :blank)))
  exprBinary (:choice
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kLt) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _ref) (:field :operator kLt) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kEq) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kNeq) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kGt) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kLte) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kGte) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kIn) (:field :rhs _expr)))
              (:prec-left 1 (:seq (:field :lhs _expr) (:field :operator kIs) (:field :rhs _expr)))
              (:prec-left 2 (:seq (:field :lhs _expr) (:field :operator kAdd) (:field :rhs _expr)))
              (:prec-left 2 (:seq (:field :lhs _expr) (:field :operator kSub) (:field :rhs _expr)))
              (:prec-left 2 (:seq (:field :lhs _expr) (:field :operator kOr) (:field :rhs _expr)))
              (:prec-left 2 (:seq (:field :lhs _expr) (:field :operator kXor) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kMul) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kFdiv) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kDiv) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kMod) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kAnd) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kShl) (:field :rhs _expr)))
              (:prec-left 3 (:seq (:field :lhs _expr) (:field :operator kShr) (:field :rhs _expr))))
  exprUnary (:choice
             (:prec-left 4 (:seq (:field :operator kNot) (:field :operand _expr)))
             (:prec-left 4 (:seq (:field :operator kAdd) (:field :operand _expr)))
             (:prec-left 4 (:seq (:field :operator kSub) (:field :operand _expr)))
             (:prec-left 4 (:seq (:field :operator kAt) (:field :operand _expr))))
  exprParens (:prec-left 5 (:seq "(" _expr ")"))
  exprBrackets (:seq
                "["
                (:choice
                 (:seq
                  (:choice (:repeat1 (:prec 0 (:seq (:choice _expr range) ","))) :blank)
                  (:choice _expr range))
                 :blank)
                "]")
  type (:choice
        (:seq
         (:choice typeref declMetaClass declEnum declSet declArray declFile declString declProcRef))
        (:seq
         (:alias (:pattern "\\{\\$if[^}]*\\}" "i") pp)
         (:choice typeref declMetaClass declEnum declSet declArray declFile declString declProcRef)
         (:repeat
          (:seq
           (:alias (:pattern "\\{\\$else[^}]*\\}" "i") pp)
           (:choice
            typeref
            declMetaClass
            declEnum
            declSet
            declArray
            declFile
            declString
            declProcRef)))
         (:alias (:pattern "\\{\\$end[^}]*\\}" "i") pp)))
  typeref (:seq
           (:field :_dummy (:choice kSpecialize :blank))
           _typeref
           (:choice (:seq kDeprecated _expr) :blank))
  _typeref (:choice identifier typerefDot typerefTpl typerefPtr)
  typerefDot (:prec-left 1
              (:seq (:field :lhs _typeref) (:field :operator kDot) (:field :rhs _typeref)))
  typerefTpl (:prec-left 1 (:seq (:field :entity _typeref) kLt (:field :args typerefArgs) kGt))
  typerefPtr (:prec-left 1 (:seq (:field :operator kHat) (:field :operand _typeref)))
  typerefArgs (:seq (:choice (:repeat1 (:prec 0 (:seq _typeref ","))) :blank) _typeref)
  genericDot (:prec-left 1
              (:seq (:field :lhs _genericName) (:field :operator kDot) (:field :rhs _genericName)))
  genericTpl (:prec-left 2 (:seq (:field :entity _genericName) kLt (:field :args genericArgs) kGt))
  _genericName (:choice identifier genericDot genericTpl)
  genericArgs (:seq (:choice (:repeat1 (:prec 0 (:seq genericArg ";"))) :blank) genericArg)
  genericArg (:seq
              (:field :name
               (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
              (:field :type (:choice (:seq ":" typeref) :blank))
              (:field :defaultValue (:choice defaultValue :blank)))
  _literal (:choice literalString literalNumber kNil kTrue kFalse)
  literalString (:repeat1 _literalString)
  _literalString (:choice (:pattern "'[^']*'") literalChar)
  literalChar (:seq "#" _literalInt)
  literalNumber (:choice _literalInt _literalFloat)
  _literalInt (:choice
               (:token-immediate (:pattern "[-+]?[0-9]+"))
               (:token-immediate (:pattern "\\$[a-fA-F0-9]+")))
  _literalFloat (:prec 10 (:pattern "[-+]?[0-9]*\\.?[0-9]+(e[+-]?[0-9]+)?"))
  range (:seq _expr ".." _expr)
  _definitions (:repeat1 _definition)
  _definition (:choice
               declTypes
               declVars
               declConsts
               defProc
               (:alias declProcFwd declProc)
               declLabels
               declUses
               declExports
               (:prec -1 blockTr))
  defProc (:seq
           (:field :header declProc)
           (:choice
            (:seq
             (:field :local (:choice _definitions :blank))
             (:field :body (:choice (:alias blockTr block) (:alias asmTr asm)))
             ";")
            (:seq
             (:alias (:pattern "\\{\\$if[^}]*\\}" "i") pp)
             (:field :local (:choice _definitions :blank))
             (:field :body (:choice (:alias blockTr block) (:alias asmTr asm)))
             ";"
             (:repeat
              (:seq
               (:alias (:pattern "\\{\\$else[^}]*\\}" "i") pp)
               (:field :local (:choice _definitions :blank))
               (:field :body (:choice (:alias blockTr block) (:alias asmTr asm)))
               ";"))
             (:alias (:pattern "\\{\\$end[^}]*\\}" "i") pp))))
  declProcFwd (:seq _declProc (:choice (:seq kForward ";") procExternal) (:repeat _procAttribute))
  _visibility (:choice kPublished kPublic kProtected kPrivate)
  _declarations (:repeat1
                 (:choice
                  declTypes
                  declVars
                  declConsts
                  declProc
                  declProp
                  (:alias declProcFwd declProc)
                  declUses
                  declLabels
                  declExports))
  _classDeclarations (:repeat1 (:choice declTypes declVars declConsts declProc declProp))
  defaultValue (:seq kEq _initializer)
  declUses (:seq
            kUses
            (:choice
             (:seq (:choice (:repeat1 (:prec 0 (:seq moduleName ","))) :blank) moduleName)
             :blank)
            ";")
  declExports (:seq
               kExports
               (:choice
                (:seq (:choice (:repeat1 (:prec 0 (:seq declExport ","))) :blank) declExport)
                :blank)
               ";")
  declTypes (:seq kType (:repeat declType))
  declVars (:seq (:choice kClass :blank) (:choice kVar kThreadvar) (:repeat declVar))
  declConsts (:seq (:choice kClass :blank) (:choice kConst kResourcestring) (:repeat declConst))
  declType (:seq
            (:choice rttiAttributes :blank)
            (:choice kGeneric :blank)
            (:field :name _genericName)
            kEq
            (:field :type
             (:choice
              (:seq (:choice kType :blank) type)
              (:choice type)
              declClass
              declIntf
              declHelper))
            ";"
            (:repeat _procAttribute))
  declProc (:seq (:choice rttiAttributes :blank) (:choice _declProc _declOperator))
  declVar (:seq
           (:choice rttiAttributes :blank)
           (:field :name
            (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
           ":"
           (:field :type type)
           (:choice (:choice (:seq kAbsolute _ref) (:field :defaultValue defaultValue)) :blank)
           ";"
           (:repeat (:choice _procAttribute procExternal)))
  declConst (:seq
             (:choice rttiAttributes :blank)
             (:field :name identifier)
             (:choice (:seq ":" (:field :type type)) :blank)
             (:field :defaultValue defaultValue)
             ";"
             (:repeat _procAttribute))
  declLabels (:seq
              kLabel
              (:seq (:choice (:repeat1 (:prec 0 (:seq declLabel ","))) :blank) declLabel)
              ";")
  declLabel (:field :name identifier)
  declExport (:seq _genericName (:repeat (:seq (:choice kName kIndex) _expr)))
  declEnum (:seq
            "("
            (:seq (:choice (:repeat1 (:prec 0 (:seq declEnumValue ","))) :blank) declEnumValue)
            ")")
  declEnumValue (:seq (:field :name identifier) (:field :value (:choice defaultValue :blank)))
  declSet (:seq kSet kOf type)
  declArray (:seq
             (:choice kPacked :blank)
             kArray
             (:choice
              (:seq
               "["
               (:choice
                (:seq
                 (:choice (:repeat1 (:prec 0 (:seq (:choice range _expr) ","))) :blank)
                 (:choice range _expr))
                :blank)
               "]")
              :blank)
             kOf
             type)
  declFile (:seq kFile (:choice (:seq kOf type) :blank))
  declString (:prec-left 0 (:seq kString (:choice (:seq "[" (:choice _expr) "]") :blank)))
  declProcRef (:prec-right 1
               (:seq
                (:choice (:seq kReference kTo) :blank)
                (:choice kProcedure kFunction)
                (:field :args (:choice declArgs :blank))
                (:choice (:seq ":" (:field :type typeref)) :blank)
                (:choice (:seq kOf kObject) :blank)))
  declMetaClass (:seq kClass kOf typeref)
  declClass (:seq
             (:choice kPacked :blank)
             (:choice kClass kRecord kObject kObjcclass kObjccategory kObjcprotocol)
             (:choice
              (:choice kAbstract kSealed (:seq kExternal (:choice (:seq kName _expr) :blank)))
              :blank)
             (:field :parent
              (:choice
               (:seq
                "("
                (:choice
                 (:seq (:choice (:repeat1 (:prec 0 (:seq typeref ","))) :blank) typeref)
                 :blank)
                ")")
               :blank))
             (:choice _declClass :blank))
  declIntf (:seq
            (:choice kPacked :blank)
            (:choice kInterface kDispInterface)
            (:field :parent
             (:choice
              (:seq
               "("
               (:choice
                (:seq (:choice (:repeat1 (:prec 0 (:seq typeref ","))) :blank) typeref)
                :blank)
               ")")
              :blank))
            (:field :guid (:choice guid :blank))
            (:choice _declClass :blank))
  declHelper (:seq
              (:choice kClass kRecord kType)
              kHelper
              (:field :parent
               (:choice
                (:seq
                 "("
                 (:choice
                  (:seq (:choice (:repeat1 (:prec 0 (:seq typeref ","))) :blank) typeref)
                  :blank)
                 ")")
                :blank))
              kFor
              typeref
              _declClass)
  guid (:prec 1 (:seq "[" _ref "]"))
  _declClass (:seq
              (:choice _declFields :blank)
              (:choice _classDeclarations :blank)
              (:repeat declSection)
              (:choice declVariant :blank)
              kEnd)
  declSection (:seq
               (:choice kStrict :blank)
               (:choice _visibility kRequired kOptional)
               (:choice _declFields :blank)
               (:choice _classDeclarations :blank))
  _declFields (:repeat1 declField)
  declField (:seq
             (:choice rttiAttributes :blank)
             (:field :name
              (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
             ":"
             (:field :type type)
             (:field :defaultValue (:choice defaultValue :blank))
             ";")
  declProp (:seq
            (:choice rttiAttributes :blank)
            (:choice kClass :blank)
            kProperty
            (:field :name identifier)
            (:field :args (:choice declPropArgs :blank))
            ":"
            (:field :type type)
            (:repeat
             (:choice
              (:seq kIndex (:field :index _expr))
              (:seq kDispId (:field :dispid _expr))
              (:seq kRead (:field :getter identifier))
              (:seq kWrite (:field :setter identifier))
              (:seq
               kImplements
               (:field :implements
                (:choice (:seq (:choice (:repeat1 (:prec 0 (:seq _expr ","))) :blank) _expr) :blank)))
              (:seq kDefault (:field :defaultValue _expr))
              (:seq kStored (:field :stored _expr))
              kNodefault))
            ";"
            (:repeat _procAttribute))
  declPropArgs (:seq
                "["
                (:choice
                 (:seq (:choice (:repeat1 (:prec 0 (:seq declArg ";"))) :blank) declArg)
                 :blank)
                "]")
  declVariant (:prec-right 0
               (:seq
                kCase
                (:field :name (:choice (:seq identifier ":") :blank))
                (:field :type typeref)
                kOf
                (:seq
                 (:choice (:repeat1 (:prec 0 (:seq declVariantClause ";"))) :blank)
                 declVariantClause)
                (:choice ";" :blank)))
  declVariantClause (:seq
                     caseLabel
                     "("
                     (:choice
                      (:seq
                       (:choice
                        (:seq
                         (:choice
                          (:repeat1 (:prec 0 (:seq (:alias declVariantField declField) ";")))
                          :blank)
                         (:alias declVariantField declField))
                        :blank)
                       (:choice (:seq ";" declVariant) :blank))
                      (:seq declVariant))
                     (:choice ";" :blank)
                     ")")
  declVariantField (:seq
                    (:field :name
                     (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
                    ":"
                    (:field :type type)
                    (:field :defaultValue (:choice defaultValue :blank)))
  _declProc (:seq
             (:choice kGeneric :blank)
             (:choice kClass :blank)
             (:choice kProcedure kFunction kConstructor kDestructor)
             (:field :name _genericName)
             (:field :args (:choice declArgs :blank))
             (:choice (:seq ":" (:field :type typeref)) :blank)
             (:field :assign (:choice defaultValue :blank))
             ";"
             (:repeat _procAttributeNoExt))
  _declOperator (:seq
                 (:choice kClass :blank)
                 kOperator
                 (:field :name _operatorName)
                 (:field :args (:choice declArgs :blank))
                 (:field :resultName (:choice identifier :blank))
                 ":"
                 (:field :type type)
                 (:field :assign (:choice defaultValue :blank))
                 ";"
                 (:repeat _procAttributeNoExt))
  operatorDot (:prec-left 0
               (:seq (:field :lhs _genericName) (:field :operator kDot) (:field :rhs operatorName)))
  _operatorName (:seq (:choice _genericName operatorName (:alias operatorDot genericDot)))
  operatorName (:choice
                kDot
                kLt
                kEq
                kNeq
                kGt
                kLte
                kGte
                kAdd
                kSub
                kMul
                kFdiv
                kDiv
                kMod
                kAssign
                kOr
                kXor
                kAnd
                kShl
                kShr
                kNot
                kIn)
  declArgs (:seq
            "("
            (:choice (:seq (:choice (:repeat1 (:prec 0 (:seq declArg ";"))) :blank) declArg) :blank)
            ")")
  declArg (:choice
           (:seq
            (:choice kVar kConst kOut kConstref)
            (:field :name
             (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
            (:choice
             (:seq ":" (:field :type type) (:field :defaultValue (:choice defaultValue :blank)))
             :blank))
           (:seq
            (:field :name
             (:seq (:choice (:repeat1 (:prec 0 (:seq identifier ","))) :blank) identifier))
            ":"
            (:field :type type)
            (:field :defaultValue (:choice defaultValue :blank))))
  _procAttribute (:choice
                  (:seq (:field :attribute procAttribute) ";")
                  (:seq
                   "["
                   (:choice
                    (:seq
                     (:choice
                      (:repeat1
                       (:prec 0 (:seq (:field :attribute (:choice procAttribute procExternal)) ",")))
                      :blank)
                     (:field :attribute (:choice procAttribute procExternal)))
                    :blank)
                   "]"
                   ";"))
  _procAttributeNoExt (:choice
                       (:seq (:field :attribute procAttribute) ";")
                       (:seq
                        "["
                        (:choice
                         (:seq
                          (:choice
                           (:repeat1
                            (:prec 0 (:seq (:field :attribute (:choice procAttribute)) ";")))
                           :blank)
                          (:field :attribute (:choice procAttribute)))
                         :blank)
                        "]"
                        ";"))
  procAttribute (:choice
                 kStatic
                 kVirtual
                 kDynamic
                 kAbstract
                 kOverride
                 kOverload
                 kReintroduce
                 kInline
                 kStdcall
                 kCdecl
                 kPascal
                 kRegister
                 kSafecall
                 kAssembler
                 kNoreturn
                 kLocal
                 kFar
                 kNear
                 kDefault
                 kNodefault
                 kDeprecated
                 kExperimental
                 (:seq (:choice (:seq kMessage (:choice kName :blank)) kDeprecated) _expr)
                 kPlatform
                 kUnimplemented
                 kCppdecl
                 kCvar
                 kMwpascal
                 kNostackframe
                 kInterrupt
                 kIocheck
                 kHardfloat
                 kSoftfloat
                 kMs_abi_default
                 kMs_abi_cdecl
                 kSaveregisters
                 kSysv_abi_default
                 kSysv_abi_cdecl
                 kVectorcall
                 kVarargs
                 kWinapi
                 kPublic
                 (:seq (:choice kExport (:seq kAlias ":") (:seq kPublic kName)) _expr)
                 (:field :dispid (:seq kDispId _expr)))
  rttiAttributes (:repeat1
                  (:seq
                   "["
                   (:choice (:seq identifier ":") :blank)
                   (:choice
                    (:seq (:choice (:repeat1 (:prec 0 (:seq _ref ","))) :blank) _ref)
                    :blank)
                   "]"))
  procExternal (:seq
                kExternal
                (:choice _expr :blank)
                (:choice (:seq (:choice kName kIndex) _expr) :blank)
                (:choice kDelayed :blank)
                ";")
  _initializer (:prec 2 (:seq (:choice _expr recInitializer arrInitializer)))
  recInitializer (:seq
                  "("
                  (:seq
                   (:choice (:repeat1 (:prec 0 (:seq recInitializerField ";"))) :blank)
                   recInitializerField)
                  ")")
  recInitializerField (:choice
                       (:seq (:field :name identifier) ":" (:field :value _initializer))
                       (:field :value _initializer))
  arrInitializer (:prec 1
                  (:seq
                   "("
                   (:seq (:choice (:repeat1 (:prec 0 (:seq _initializer ","))) :blank) _initializer)
                   ")"))
  kProgram (:pattern "program" "i")
  kLibrary (:pattern "library" "i")
  kUnit (:pattern "unit" "i")
  kUses (:pattern "uses" "i")
  kInterface (:pattern "interface" "i")
  kDispInterface (:pattern "dispinterface" "i")
  kImplementation (:pattern "implementation" "i")
  kInitialization (:pattern "initialization" "i")
  kFinalization (:pattern "finalization" "i")
  kEndDot "."
  kBegin (:pattern "begin" "i")
  kEnd (:pattern "end" "i")
  kAsm (:pattern "asm" "i")
  kVar (:pattern "var" "i")
  kThreadvar (:pattern "threadvar" "i")
  kConst (:pattern "const" "i")
  kConstref (:pattern "constref" "i")
  kResourcestring (:pattern "resourcestring" "i")
  kOut (:pattern "out" "i")
  kType (:pattern "type" "i")
  kLabel (:pattern "label" "i")
  kExports (:pattern "exports" "i")
  kAbsolute (:pattern "absolute" "i")
  kProperty (:pattern "property" "i")
  kRead (:pattern "read" "i")
  kWrite (:pattern "write" "i")
  kImplements (:pattern "implements" "i")
  kDefault (:pattern "default" "i")
  kNodefault (:pattern "nodefault" "i")
  kStored (:pattern "stored" "i")
  kIndex (:pattern "index" "i")
  kDispId (:pattern "dispid" "i")
  kClass (:pattern "class" "i")
  kObject (:pattern "object" "i")
  kRecord (:pattern "record" "i")
  kObjcclass (:pattern "objcclass" "i")
  kObjccategory (:pattern "objccategory" "i")
  kObjcprotocol (:pattern "objcprotocol" "i")
  kArray (:pattern "array" "i")
  kFile (:pattern "file" "i")
  kString (:pattern "string" "i")
  kSet (:pattern "set" "i")
  kOf (:pattern "of" "i")
  kHelper (:pattern "helper" "i")
  kPacked (:pattern "packed" "i")
  kGeneric (:pattern "generic" "i")
  kSpecialize (:pattern "specialize" "i")
  kDot "."
  kLt "<"
  kEq "="
  kNeq "<>"
  kGt ">"
  kLte "<="
  kGte ">="
  kAdd "+"
  kSub "-"
  kMul "*"
  kFdiv "/"
  kAt "@"
  kHat "^"
  kAssign ":="
  kAssignAdd "+="
  kAssignSub "-="
  kAssignMul "*="
  kAssignDiv "/="
  kOr (:pattern "or" "i")
  kXor (:pattern "xor" "i")
  kDiv (:pattern "div" "i")
  kMod (:pattern "mod" "i")
  kAnd (:pattern "and" "i")
  kShl (:pattern "shl" "i")
  kShr (:pattern "shr" "i")
  kNot (:pattern "not" "i")
  kIs (:pattern "is" "i")
  kAs (:pattern "as" "i")
  kIn (:pattern "in" "i")
  kFor (:pattern "for" "i")
  kTo (:pattern "to" "i")
  kDownto (:pattern "downto" "i")
  kIf (:pattern "if" "i")
  kThen (:pattern "then" "i")
  kElse (:pattern "else" "i")
  kDo (:pattern "do" "i")
  kWhile (:pattern "while" "i")
  kRepeat (:pattern "repeat" "i")
  kUntil (:pattern "until" "i")
  kTry (:pattern "try" "i")
  kExcept (:pattern "except" "i")
  kFinally (:pattern "finally" "i")
  kRaise (:pattern "raise" "i")
  kOn (:pattern "on" "i")
  kCase (:pattern "case" "i")
  kWith (:pattern "with" "i")
  kGoto (:pattern "goto" "i")
  kFunction (:pattern "function" "i")
  kProcedure (:pattern "procedure" "i")
  kConstructor (:pattern "constructor" "i")
  kDestructor (:pattern "destructor" "i")
  kOperator (:pattern "operator" "i")
  kReference (:pattern "reference" "i")
  kPublished (:pattern "published" "i")
  kPublic (:pattern "public" "i")
  kProtected (:pattern "protected" "i")
  kPrivate (:pattern "private" "i")
  kStrict (:pattern "strict" "i")
  kRequired (:pattern "required" "i")
  kOptional (:pattern "optional" "i")
  kForward (:pattern "forward" "i")
  kStatic (:pattern "static" "i")
  kVirtual (:pattern "virtual" "i")
  kAbstract (:pattern "abstract" "i")
  kSealed (:pattern "seled" "i")
  kDynamic (:pattern "dynamic" "i")
  kOverride (:pattern "override" "i")
  kOverload (:pattern "overload" "i")
  kReintroduce (:pattern "reintroduce" "i")
  kInherited (:pattern "inherited" "i")
  kInline (:pattern "inline" "i")
  kStdcall (:pattern "stdcall" "i")
  kCdecl (:pattern "cdecl" "i")
  kCppdecl (:pattern "cppdecl" "i")
  kPascal (:pattern "pascal" "i")
  kRegister (:pattern "register" "i")
  kMwpascal (:pattern "mwpascal" "i")
  kExternal (:pattern "external" "i")
  kName (:pattern "name" "i")
  kMessage (:pattern "message" "i")
  kDeprecated (:pattern "deprecated" "i")
  kExperimental (:pattern "experimental" "i")
  kPlatform (:pattern "platform" "i")
  kUnimplemented (:pattern "unimplemented" "i")
  kCvar (:pattern "cvar" "i")
  kExport (:pattern "export" "i")
  kFar (:pattern "far" "i")
  kNear (:pattern "near" "i")
  kSafecall (:pattern "safecal" "i")
  kAssembler (:pattern "assembler" "i")
  kNostackframe (:pattern "nostackframe" "i")
  kInterrupt (:pattern "interrupt" "i")
  kNoreturn (:pattern "noreturn" "i")
  kIocheck (:pattern "iocheck" "i")
  kLocal (:pattern "local" "i")
  kHardfloat (:pattern "hardfloat" "i")
  kSoftfloat (:pattern "softfloat" "i")
  kMs_abi_default (:pattern "ms_abi_default" "i")
  kMs_abi_cdecl (:pattern "ms_abi_cdecl" "i")
  kSaveregisters (:pattern "saveregisters" "i")
  kSysv_abi_default (:pattern "sysv_abi_default" "i")
  kSysv_abi_cdecl (:pattern "sysv_abi_cdecl" "i")
  kVectorcall (:pattern "vectorcall" "i")
  kVarargs (:pattern "varargs" "i")
  kWinapi (:pattern "winapi" "i")
  kAlias (:pattern "alias" "i")
  kDelayed (:pattern "delayed" "i")
  kNil (:pattern "nil" "i")
  kTrue (:pattern "true" "i")
  kFalse (:pattern "false" "i")
  kIfdef (:pattern "ifdef" "i")
  kIfndef (:pattern "ifndef" "i")
  kEndif (:pattern "endif" "i")
  identifier (:pattern "[&]?[a-zA-Z_]+[0-9_a-zA-Z]*")
  _space (:pattern "[\\s\\r\\n\\t]+")
  pp (:pattern "\\{\\$[^}]*\\}")
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "{" (:pattern "([^$}][^}]*)?") "}")
            (:pattern "[(][*]([^*]*[*]+[^)*])*[^*]*[*]+[)]")))}}
