# grammar.janet adds the `#![enable(...)]` extension header upstream's
# grammar lacks.

{:name "ron"
 :extensions [".ron"]
 :line-comment "//"
 :queries {:highlights ["ron/highlights.janet"
                        "ron/upstream/highlights.janet"]}
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
}
