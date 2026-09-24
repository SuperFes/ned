# No :line-comment -- XML only has block comments (<!-- -->).

{:name "xml"
 :extensions [".xml" ".xsd" ".xsl" ".xslt" ".svg"]
 :injection-aliases ["svg" "xsl" "plist"]
 :block-comment ["<!--" "-->"]
 # Upstream captures CDATA's "<![CDATA[" and "]]>" as markup.heading.
 :capture-classes {"markup.heading" :punctuation}
}
