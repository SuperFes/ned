{:name "pascal"
 :extensions [".pas" ".pp" ".lpr" ".dpr" ".dpk" ".inc"]
 :injection-aliases ["delphi" "pas"]
 :line-comment "//"
 :signature-template "procedure NedSig({});\nbegin end;"
 :lsp-root-markers ["*.lpi" "*.lpk" "*.dproj" "*.dpr"]
 :import-resolution {:extensions ["pas" "pp" "p"] :module-join "." :flat-modules true}
 :not-applicable {:injections "nothing in it is written in another language"}
}
