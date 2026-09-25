{:name "astro"
 :extensions [".astro"]
 :lsp-root-markers ["package.json"]
 :block-comment ["<!--" "-->"]
 # Stylesheet spellings: `#f0a` is a colour here rather than the start of a
 # comment, and `tomato` a colour rather than an identifier
 # (Editor/ColorLiteral.h).
 :color-literals [:short-hex :named]
 # Upstream's injections inherit nvim's html_tags (plain <style> as css);
 # listed after astro's own so its typescript <script> keeps priority.
 :queries {:injections ["astro/upstream/injections.janet"
                        "html/upstream/html_tags/injections.janet"]}
 # The outline lists what the embedded scripts define; breadcrumbs follow
 # the element nesting.
 :injected-symbols true
 :sticky-scroll-from-folds true
}
