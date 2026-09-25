{:name "erlang"
 :extensions [".erl" ".hrl" ".escript"]
 :injection-aliases ["erl"]
 :filenames ["rebar.config" "app.src" "sys.config"]
 :line-comment "%"
 :lsp-root-markers ["rebar.config" "erlang.mk"]
 :import-resolution {:extensions ["hrl" "erl"] :source-roots ["include" "apps" "_build/default/lib" "deps"]}
 :not-applicable {:injections "nothing in it is written in another language"}
}
