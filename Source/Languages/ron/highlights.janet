#; Highlight query, ned-authored: what grammar.janet adds to upstream's --
#; the `#![enable(...)]` extension header.

(extension_attribute
  ["#" "!" "[" "]" "enable"] @attribute
  (identifier) @attribute)
