# Single-file components: the script and style blocks are injections into
# their declared languages.
{:name "vue"
 :extensions [".vue"]
 # Stylesheet spellings: `#f0a` is a colour here rather than the start of a
 # comment, and `tomato` a colour rather than an identifier
 # (Editor/ColorLiteral.h).
 :color-literals [:short-hex :named]
 # The upstream queries are deltas whose first line says `inherits: html` or
 # `inherits: html_tags`; discovery doesn't read that, so the base is named here.
 :queries {:highlights ["html/upstream/html_tags/highlights.janet"
                        "vue/upstream/highlights.janet"]
           :injections ["html/upstream/html_tags/injections.janet"
                        "vue/upstream/injections.janet"]}
}
