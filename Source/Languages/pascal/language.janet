{:name "pascal"
 :extensions [".pas" ".pp" ".lpr" ".dpr" ".dpk" ".inc"]
 :injection-aliases ["delphi" "pas"]
 :line-comment "//"
 :signature-template "procedure NedSig({});\nbegin end;"
 :lsp-root-markers ["*.lpi" "*.lpk" "*.dproj" "*.dpr"]
 :not-applicable {:injections "nothing in it is written in another language"}
}
