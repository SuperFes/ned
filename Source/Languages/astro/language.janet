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
 # Their <script> imports resolve and follow moves like the script's own
 # language's would (SvelteKit's $lib and Vite's @/ are the conventional aliases).
 :injected-imports true
 :import-resolution {:extensions ["ts" "js" "astro" "tsx" "jsx" "mjs"] :index-basenames ["index"] :search-package-dirs true}
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :tests      "tests live in its scripts' own language"
                  :signatures "its functions live in embedded scripts, in their own language"}
}
