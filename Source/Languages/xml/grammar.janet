# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "xml"
 :word Name
 :extras []
 :conflicts [[AttlistDecl AttDef]]
 :precedences []
 :externals [PITarget
             _pi_content
             Comment
             CharData
             CData
             "xml-model"
             "xml-stylesheet"
             _start_tag_name
             _end_tag_name
             _erroneous_end_name
             "/>"]
 :inline []
 :supertypes [_markupdecl _AttType _EnumeratedType _EntityDecl _Reference]
 :rules
 {document (:prec 2
            (:seq
             (:choice _S :blank)
             (:choice prolog :blank)
             (:field :root element)
             (:repeat _Misc)))
  prolog (:choice
          (:seq XMLDecl (:repeat _Misc))
          (:seq (:choice XMLDecl :blank) (:repeat _Misc) doctypedecl (:repeat _Misc))
          (:repeat1 _Misc))
  _Misc (:choice PI StyleSheetPI XmlModelPI Comment _S)
  XMLDecl (:seq
           "<?"
           "xml"
           _VersionInfo
           (:choice _EncodingDecl :blank)
           (:choice _SDDecl :blank)
           (:choice _S :blank)
           "?>")
  _SDDecl (:seq
           _S
           "standalone"
           _Eq
           (:choice (:seq "'" (:choice "yes" "no") "'") (:seq "\"" (:choice "yes" "no") "\"")))
  doctypedecl (:seq
               "<!"
               "DOCTYPE"
               _S
               Name
               (:choice (:seq _S ExternalID) :blank)
               (:choice _S :blank)
               (:choice
                (:seq "[" (:choice (:choice _intSubset _S) :blank) "]" (:choice _S :blank))
                :blank)
               ">")
  _intSubset (:repeat1 (:seq (:choice _S :blank) _markupdecl _DeclSep))
  element (:choice EmptyElemTag (:seq STag (:choice content :blank) ETag))
  EmptyElemTag (:seq
                "<"
                (:alias _start_tag_name Name)
                (:repeat (:seq _S Attribute))
                (:choice _S :blank)
                "/>")
  Attribute (:seq Name _Eq AttValue)
  STag (:seq
        "<"
        (:alias _start_tag_name Name)
        (:repeat (:seq _S Attribute))
        (:choice _S :blank)
        ">")
  ETag (:seq "</" (:alias _end_tag_name Name) (:choice _S :blank) ">")
  _ErroneousETag (:seq "</" (:alias _erroneous_end_name ERROR) (:choice _S :blank) ">")
  content (:repeat1 (:choice CharData element _Reference CDSect PI Comment))
  CDSect (:prec-left 0 (:seq CDStart (:choice CData :blank) "]]>"))
  CDStart (:seq "<![" "CDATA" "[")
  StyleSheetPI (:seq "<?" "xml-stylesheet" (:repeat (:seq _S PseudoAtt)) (:choice _S :blank) "?>")
  XmlModelPI (:seq "<?" "xml-model" (:repeat (:seq _S PseudoAtt)) (:choice _S :blank) "?>")
  PseudoAtt (:seq Name _Eq PseudoAttValue)
  PseudoAttValue (:choice
                  (:seq
                   "\""
                   (:field :content (:repeat (:choice (:pattern "[^<&\"]") _Reference)))
                   "\"")
                  (:seq
                   "'"
                   (:field :content (:repeat (:choice (:pattern "[^<&']") _Reference)))
                   "'"))
  _markupdecl (:choice elementdecl AttlistDecl _EntityDecl NotationDecl PI Comment)
  _DeclSep (:choice PEReference _S)
  elementdecl (:seq
               "<!"
               "ELEMENT"
               _S
               (:choice Name PEReference)
               _S
               contentspec
               (:choice _S :blank)
               ">")
  contentspec (:choice "EMPTY" "ANY" Mixed children PEReference)
  Mixed (:choice
         (:seq
          "("
          (:choice _S :blank)
          (:choice "#PCDATA" PEReference)
          (:repeat (:seq (:choice _S :blank) "|" (:choice _S :blank) (:choice Name PEReference)))
          (:choice _S :blank)
          (:repeat (:seq PEReference (:choice _S :blank)))
          ")"
          "*")
         (:prec -1
          (:seq
           "("
           (:choice _S :blank)
           (:choice "#PCDATA" PEReference)
           (:choice _S :blank)
           (:repeat (:seq PEReference (:choice _S :blank)))
           ")")))
  children (:prec 1 (:seq _choice (:choice (:choice "?" "*" "+") :blank)))
  _cp (:prec-left 0
       (:seq (:choice Name _choice PEReference) (:choice (:choice "?" "*" "+") :blank)))
  _choice (:seq
           "("
           (:choice _S :blank)
           _cp
           (:repeat (:seq (:choice _S :blank) (:choice "|" ",") (:choice _S :blank) _cp))
           (:repeat (:seq (:choice _S :blank) PEReference))
           (:choice _S :blank)
           ")")
  AttlistDecl (:seq
               "<!"
               "ATTLIST"
               _S
               (:choice Name PEReference)
               (:repeat (:choice AttDef (:seq _S PEReference)))
               (:choice _S :blank)
               ">")
  AttDef (:prec-right 0
          (:seq _S (:choice Name PEReference) _S _AttType (:choice (:seq _S DefaultDecl) :blank)))
  _AttType (:choice StringType TokenizedType _EnumeratedType PEReference)
  StringType "CDATA"
  TokenizedType (:token (:choice "ID" "IDREF" "IDREFS" "ENTITY" "ENTITIES" "NMTOKEN" "NMTOKENS"))
  _EnumeratedType (:choice NotationType Enumeration)
  NotationType (:seq
                "NOTATION"
                _S
                "("
                (:choice _S :blank)
                (:choice Name PEReference)
                (:repeat (:seq (:choice _S :blank) "|" (:choice _S :blank)))
                (:choice Name PEReference)
                (:choice _S :blank)
                ")")
  Enumeration (:seq
               "("
               (:choice _S :blank)
               Nmtoken
               (:repeat (:seq (:choice _S :blank) "|" (:choice _S :blank) Nmtoken))
               (:choice _S :blank)
               ")")
  DefaultDecl (:choice
               "#REQUIRED"
               "#IMPLIED"
               (:seq (:choice (:seq "#FIXED" _S) :blank) AttValue)
               PEReference)
  _EntityDecl (:choice GEDecl PEDecl)
  GEDecl (:seq
          "<!"
          "ENTITY"
          _S
          (:choice Name PEReference)
          _S
          (:choice EntityValue (:seq ExternalID (:choice NDataDecl :blank)))
          (:choice _S :blank)
          ">")
  PEDecl (:seq
          "<!"
          "ENTITY"
          _S
          "%"
          _S
          Name
          _S
          (:choice EntityValue ExternalID)
          (:choice _S :blank)
          ">")
  EntityValue (:choice
               (:seq
                "\""
                (:field :content (:repeat (:choice (:pattern "[^<%&\"]") PEReference _Reference)))
                "\"")
               (:seq
                "'"
                (:field :content (:repeat (:choice (:pattern "[^<%&']") PEReference _Reference)))
                "'"))
  NDataDecl (:seq _S "NDATA" _S (:choice Name PEReference))
  NotationDecl (:seq
                "<!"
                "NOTATION"
                _S
                (:choice Name PEReference)
                _S
                (:choice ExternalID PublicID)
                (:choice _S :blank)
                ">")
  PEReference (:seq "%" Name ";")
  _S (:pattern "[ \\t\\r\\n]+")
  Name (:pattern "[a-zA-Z_][a-zA-Z0-9_:.·-]*")
  Nmtoken (:pattern "[a-zA-Z0-9_:.·-]+")
  _Reference (:choice EntityRef CharRef)
  EntityRef (:seq "&" Name ";")
  CharRef (:choice (:seq "&#" (:pattern "[0-9]+") ";") (:seq "&#x" (:pattern "[0-9a-fA-F]+") ";"))
  AttValue (:choice
            (:seq "\"" (:field :content (:repeat (:choice (:pattern "[^<&\"]") _Reference))) "\"")
            (:seq "'" (:field :content (:repeat (:choice (:pattern "[^<&']") _Reference))) "'"))
  ExternalID (:choice
              (:seq "SYSTEM" _S SystemLiteral)
              (:seq "PUBLIC" _S PubidLiteral _S SystemLiteral))
  PublicID (:prec-right 0 (:seq (:choice "PUBLIC" PEReference) _S PubidLiteral))
  SystemLiteral (:choice
                 (:seq "\"" (:alias (:pattern "[^\"]*") URI) "\"")
                 (:seq "'" (:alias (:pattern "[^']*") URI) "'"))
  PubidLiteral (:choice
                (:seq "\"" (:pattern "[ \\r\\na-zA-Z0-9\\-'()+,./:=?;!*#@$_%]*") "\"")
                (:seq "'" (:pattern "[ \\r\\na-zA-Z0-9\\-()+,./:=?;!*#@$_%]*") "'"))
  _VersionInfo (:seq
                _S
                "version"
                _Eq
                (:choice (:seq "'" VersionNum "'") (:seq "\"" VersionNum "\"")))
  VersionNum (:pattern "1\\.[0-9]+")
  _EncodingDecl (:seq _S "encoding" _Eq (:choice (:seq "'" EncName "'") (:seq "\"" EncName "\"")))
  EncName (:pattern "[A-Za-z][A-Za-z0-9._\\-]*")
  PI (:seq "<?" PITarget (:choice (:seq _S _pi_content) :blank) "?>")
  _Eq (:seq (:choice _S :blank) "=" (:choice _S :blank))}}
