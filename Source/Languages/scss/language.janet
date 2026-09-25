{:name "scss"
 :extensions [".scss"]
 # Stylesheet spellings: `#f0a` is a colour here rather than the start of a
 # comment, and `tomato` a colour rather than an identifier
 # (Editor/ColorLiteral.h).
 :color-literals [:short-hex :named]
 :line-comment "//"
 # The upstream query is a delta over CSS's (nvim's `inherits: css`).
 :queries {:highlights ["css/upstream/highlights.janet"
                        "scss/upstream/highlights.janet"
                        "scss/highlights.janet"]}
 :import-resolution {:extensions ["scss" "sass" "css"] :index-basenames ["_index" "index"] :partial-prefix "_"}
}
