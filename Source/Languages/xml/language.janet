# No :line-comment -- XML only has block comments (<!-- -->).

{:name "xml"
 :extensions [".xml" ".xsd" ".xsl" ".xslt" ".svg"]
 :injection-aliases ["svg" "xsl" "plist"]
 :block-comment ["<!--" "-->"]
 # Upstream captures CDATA's "<![CDATA[" and "]]>" as markup.heading.
 :capture-classes {"markup.heading" :punctuation}
 :not-applicable {:continuation "markup; nothing continues across lines"
                  :locals       "no bindings to scope or rename"
                  :tags         "the element tree is the document"
                  :injections   "nothing in it is written in another language"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "no user-defined functions with parameter lists"
                  :lsp-root     "no project file of its own; the root falls through to the project's"}
}
