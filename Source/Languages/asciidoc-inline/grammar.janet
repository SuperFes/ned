# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "asciidoc_inline"
 :extras [(:pattern "\\s")]
 :conflicts [[roled_text _punctuation]]
 :precedences [[autolink _punctuation]]
 :externals [_eof _hard_wrap_plus _emphasis_begin _italic_begin _monospace_begin _highlight_begin]
 :inline []
 :supertypes []
 :rules
 {inline (:repeat inline_element)
  inline_element (:choice
                  replacement
                  word
                  autolink
                  passthrough
                  macro_passthrough
                  _punctuation
                  xref
                  roled_text
                  emphasis
                  ltalic
                  monospace
                  highlight
                  superscript
                  subscript
                  inline_macro
                  stem_macro
                  footnote
                  index_term
                  index_term2
                  id_assignment
                  intrinsic_attributes_pair
                  attribute_reference
                  hard_wrap)
  autolink (:choice uri labled_uri (:seq "\"" uri "\"") (:seq "\"" labled_uri "\""))
  labled_uri (:prec 1 (:seq uri (:seq "[" (:choice uri_label :blank) "]")))
  uri_label (:repeat1 (:choice (:pattern "[^\\]]") "\\]" replacement))
  uri (:choice link_url email)
  link_url (:seq
            (:choice (:seq (:pattern "\\w[\\w\\d+.-][\\w\\d+.-]*:\\/\\/")) (:pattern "www\\." "i"))
            (:prec-right 0 (:seq _uri_segment (:repeat (:seq "." _uri_segment)))))
  _uri_segment (:pattern "[^\\.\\s\\[\\\"]+")
  email (:pattern "(?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|\"(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21\\x23-\\x5b\\x5d-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])*\")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\\.){3}(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21-\\x5a\\x53-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])+)\\])")
  id_assignment (:choice (:seq "[#" id "]") (:seq "[[" id (:choice (:seq "," reftext) :blank) "]]"))
  id (:repeat1 (:choice (:pattern "[^,\\]]") "\\," "\\]"))
  reftext (:repeat1 (:choice (:pattern "[^\\]]") "\\]"))
  macro_name (:choice
              "kbd"
              "btn"
              "image"
              "audio"
              "video"
              "icon"
              "link"
              "mailto"
              "menu"
              "anchor"
              "xref"
              "ifdef"
              "ifndef"
              "ifeval"
              "endif"
              "indexterm2"
              "indexterm"
              "a2s"
              "barcode"
              "blockdiag"
              "bpmn"
              "bytefield"
              "d2"
              "dbml"
              "diagrams"
              "ditaa"
              "dpic"
              "erd"
              "gnuplot"
              "graphviz"
              "graphviz"
              "lilypond"
              "meme"
              "mermaid"
              "msc"
              "nomnoml"
              "pikchr"
              "plantuml"
              "shaape"
              "smcat"
              "structurizr"
              "svgbob"
              "symbolator"
              "syntrax"
              "tikz"
              "umlet"
              "vega"
              "wavedrom")
  inline_macro (:seq
                macro_name
                (:token-immediate ":")
                (:choice (:choice target) :blank)
                "["
                (:choice attr :blank)
                "]")
  target (:repeat1
          (:choice
           (:pattern "[^\\[]")
           "\\["
           replacement
           escaped_sequence
           passthrough
           macro_passthrough))
  attr (:repeat1
        (:choice
         (:pattern "[^\\]]")
         "\\]"
         replacement
         autolink
         escaped_sequence
         (:prec-left -1 "\"")))
  _footnotename (:choice "footnote" "footnoteref")
  footnote (:seq
            (:alias _footnotename macro_name)
            (:token-immediate ":")
            (:choice target :blank)
            "["
            (:choice attr :blank)
            "]")
  _stem_attr (:repeat1 (:choice (:pattern "[^\\]]") "\\]"))
  _stem_name (:choice "latexmath" "stem" "asciimath")
  stem_macro (:seq
              (:alias _stem_name macro_name)
              (:token-immediate ":")
              (:choice target :blank)
              "["
              (:choice (:alias _stem_attr attr) :blank)
              "]")
  macro_passthrough (:seq
                     (:alias "pass" macro_name)
                     (:token-immediate ":")
                     (:choice target :blank)
                     "["
                     (:choice (:alias _stem_attr attr) :blank)
                     "]")
  replacement (:token (:choice "(C)" "(R)" "(TM)" "..." "`'" "-&gt;" "=&gr;" "&lt;-" "&lt;="))
  intrinsic_attributes_pair (:seq "{" intrinsic_attributes "}")
  intrinsic_attributes (:token
                        (:choice
                         "startsb"
                         "endsb"
                         "vbar"
                         "caret"
                         "asterisk"
                         "tilde"
                         "plus"
                         "backslash"
                         "backtick"
                         "blank"
                         "empty"
                         "sp"
                         "two-colons"
                         "two-semicolons"
                         "nbsp"
                         "deg"
                         "zwsp"
                         "quot"
                         "apos"
                         "lsquo"
                         "rsquo"
                         "ldquo"
                         "rdquo"
                         "wj"
                         "brvbar"
                         "pp"
                         "cpp"
                         "amp"
                         "lt"
                         "gt"))
  attribute_reference (:seq "{" attribute_name "}")
  attribute_name (:token (:pattern "[A-Za-z0-9_][A-Za-z0-9_-]*"))
  word (:choice super_escape _character _fallback_token escaped_sequence "f")
  _fallback_token (:choice macro_name _stem_name _footnotename)
  super_escape "\\\\"
  _character (:token (:prec -1 (:pattern "[^f\\s!\"#$%&'()*+,-./:;<=>?@[\\\\]^_`{|}~]+")))
  escaped_sequence (:choice
                    (:token "\\+++")
                    (:token "\\``")
                    (:token "\\**")
                    (:token "\\$$")
                    (:token "\\##")
                    (:token "\\__")
                    (:token "\\<<")
                    (:token "\\[[")
                    (:token "\\++")
                    (:token "\\kbd")
                    (:token "\\btn")
                    (:token "\\image")
                    (:token "\\audio")
                    (:token "\\video")
                    (:token "\\icon")
                    (:token "\\pass")
                    (:token "\\link")
                    (:token "\\mailto")
                    (:token "\\menu")
                    (:token "\\stem")
                    (:token "\\latexmath")
                    (:token "\\asciimath")
                    (:token "\\footnote")
                    (:token "\\footnoteref")
                    (:token "\\anchor")
                    (:token "\\xref")
                    (:token "\\ifdef")
                    (:token "\\ifndef")
                    (:token "\\ifeval")
                    (:token "\\endif")
                    (:pattern "\\\\."))
  _punctuation (:choice
                "!"
                "\""
                "#"
                "$"
                "%"
                "&"
                "'"
                "("
                ")"
                "*"
                "+"
                ","
                "-"
                "."
                "/"
                ":"
                ";"
                "<"
                "="
                ">"
                "?"
                "@"
                "["
                "\\"
                "]"
                "^"
                "_"
                "`"
                "{"
                "|"
                "}"
                "~")
  passthrough (:choice
               (:token
                (:seq
                 (:prec 1 "+")
                 (:choice
                  (:pattern "[^+ \\t\\r\\n]")
                  (:seq
                   (:pattern "[^+ \\t\\r\\n]")
                   (:repeat (:choice (:pattern "[^+\\r\\n]") "\\+"))
                   (:pattern "[^+ \\t\\r\\n]")))
                 "+"))
               (:seq (:token (:prec 1 "++")) (:repeat (:choice (:pattern "[^+\\r\\n]") "\\+")) "++")
               (:seq "$$" (:repeat1 (:choice (:pattern "[^$\\r\\n]") "\\$" "\\$$")) "$$"))
  xref (:seq
        "<<"
        (:alias (:repeat1 (:choice (:pattern "[^,>]") "\\," "\\>")) id)
        (:choice (:seq "," (:alias (:repeat1 (:choice (:pattern "[^>]") "\\>")) reftext)) :blank)
        ">>")
  roled_text (:seq "[" (:repeat1 (:seq "." role)) "]" highlight)
  role (:token (:pattern "[A-Za-z0-9_][A-Za-z0-9_-]*"))
  emphasis (:choice
            (:seq
             _emphasis_begin
             (:repeat (:choice (:pattern "[^*\\r\\n]") "\\*" ltalic monospace highlight))
             "*")
            (:seq
             (:token (:prec 1 "**"))
             (:repeat (:choice (:pattern "[^*\\r\\n]") "\\*" ltalic monospace highlight))
             "**"))
  ltalic (:choice
          (:seq
           _italic_begin
           (:repeat (:choice (:pattern "[^_\\r\\n]") "\\_" emphasis monospace highlight))
           "_")
          (:seq
           (:token (:prec 1 "__"))
           (:repeat (:choice (:pattern "[^_\\r\\n]") "\\_" emphasis monospace highlight))
           "__"))
  monospace (:choice
             (:seq _monospace_begin (:repeat (:choice (:pattern "[^`\\r\\n]") "\\`")) "`")
             (:seq (:token (:prec 1 "``")) (:repeat (:choice (:pattern "[^`\\r\\n]") "\\`")) "``"))
  highlight (:choice
             (:seq _highlight_begin (:repeat (:choice (:pattern "[^#\\r\\n]") "\\#")) "#")
             (:seq (:token (:prec 1 "##")) (:repeat (:choice (:pattern "[^#\\r\\n]") "\\#")) "##"))
  superscript (:seq (:token (:prec 1 "^")) (:repeat1 (:choice (:pattern "[^\\s^]") "\\^")) "^")
  subscript (:seq (:token (:prec 1 "~")) (:repeat1 (:choice (:pattern "[^\\s~]") "\\~")) "~")
  index_term2 (:seq "((" term (:choice (:seq "," term) :blank) "))")
  index_term (:seq
              "((("
              term
              (:choice (:seq "," term) :blank)
              (:choice (:seq "," term) :blank)
              ")))")
  term (:repeat1 (:choice (:pattern "[^,)]") "\\," "\\)"))
  hard_wrap _hard_wrap_plus}}
