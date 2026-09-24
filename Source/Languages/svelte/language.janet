{:name "svelte"
 :extensions [".svelte"]
 :block-comment ["<!--" "-->"]
 # Stylesheet spellings: `#f0a` is a colour here rather than the start of a
 # comment, and `tomato` a colour rather than an identifier
 # (Editor/ColorLiteral.h).
 :color-literals [:short-hex :named]
 # The upstream queries are deltas whose first line says `inherits: html` or
 # `inherits: html_tags`; discovery doesn't read that, so the base is named here.
 :queries {:highlights ["html/upstream/html_tags/highlights.janet"
                        "svelte/upstream/highlights.janet"]
           :injections ["html/upstream/html_tags/injections.janet"
                        "svelte/upstream/injections.janet"]}
}
