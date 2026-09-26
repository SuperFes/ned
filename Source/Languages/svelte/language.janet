{:name "svelte"
 :extensions [".svelte"]
 :lsp-root-markers ["package.json"]
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
 # The outline lists what the embedded scripts define; breadcrumbs follow
 # the element nesting.
 :injected-symbols true
 # Their <script> imports resolve and follow moves like the script's own
 # language's would (SvelteKit's $lib and Vite's @/ are the conventional aliases).
 :injected-imports true
 # Rename reads their locals too; a name the script binds at its top level
 # declines, since the markup may use it.
 :injected-locals true
 :import-resolution {:extensions ["ts" "js" "svelte" "mjs"] :index-basenames ["index"] :search-package-dirs true
                     :root-prefixes [["$lib/" "src/lib"]]}
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :tests      "tests live in its scripts' own language"
                  :signatures "its functions live in embedded scripts, in their own language"
                  :format     "a markup host: its scripts and styles format as their own languages, and its markup has nothing the rules place"}
}
