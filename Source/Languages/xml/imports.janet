# DTDs, stylesheets and XIncludes; a public http:// system identifier is no
# file (@import.link).
(ExternalID (SystemLiteral (URI) @import.link)) @import.statement
(StyleSheetPI
  (PseudoAtt (Name) @_name (PseudoAttValue) @import.link)
  (:eq? @_name "href")) @import.statement
(EmptyElemTag
  (Name) @_tag
  (Attribute (Name) @_name (AttValue) @import.link)
  (:match? @_tag "^(\\w+:)?include$")
  (:eq? @_name "href")) @import.statement
(STag
  (Name) @_tag
  (Attribute (Name) @_name (AttValue) @import.link)
  (:match? @_tag "^(\\w+:)?include$")
  (:eq? @_name "href")) @import.statement
(Attribute
  (Name) @_name
  (AttValue) @import.link
  (:match? @_name "^(\\w+:)?noNamespaceSchemaLocation$")) @import.statement
