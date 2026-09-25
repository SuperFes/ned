{:name "hcl"
 :extensions [".hcl" ".tf" ".tfvars"]
 :injection-aliases ["terraform" "tf"]
 :line-comment "#"
 :lsp-root-markers [".terraform" ".terraform.lock.hcl"]
 # The outline stops at top-level blocks; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :import-resolution {:extensions ["tf"] :index-basenames ["main"]}
 :not-applicable {:locals     "names are module-wide across files (`var.x`, `local.x`)"
                  :injections "nothing in it is written in another language"
                  :signatures "no user-defined functions"}
}
