#; Local-binding query: nvim-treesitter's Ruby query (upstream/locals.janet)
#; with one change -- an assignment inside a block to a local the block can
#; already see writes that local rather than shadowing it, which upstream
#; can't express.

((method) @local.scope
 (:set! local.scope-inherits false))

[
  (lambda)
  (block)
  (do_block)
] @local.scope

(block_parameter (identifier) @local.definition)
(block_parameters (identifier) @local.definition)
(destructured_parameter (identifier) @local.definition)
(hash_splat_parameter (identifier) @local.definition)
(lambda_parameters (identifier) @local.definition)
(method_parameters (identifier) @local.definition)
(splat_parameter (identifier) @local.definition)

(keyword_parameter name: (identifier) @local.definition)
(optional_parameter name: (identifier) @local.definition)

(identifier) @local.reference

#; An assignment inside a block to a local the block can already see writes
#; that local; only otherwise does it introduce one (`local.assignment`).
((assignment left: (identifier) @local.definition)
 (:set! local.assignment "true"))
((operator_assignment left: (identifier) @local.definition)
 (:set! local.assignment "true"))
(left_assignment_list (identifier) @local.definition)
(rest_assignment (identifier) @local.definition)
(destructured_left_assignment (identifier) @local.definition)
