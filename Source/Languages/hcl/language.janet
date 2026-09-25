{:name "hcl"
 :extensions [".hcl" ".tf" ".tfvars"]
 :injection-aliases ["terraform" "tf"]
 :line-comment "#"
 :lsp-root-markers [".terraform" ".terraform.lock.hcl"]
 # The outline stops at top-level blocks; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
}
