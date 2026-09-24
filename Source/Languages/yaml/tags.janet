#; Symbol-kind query, ned-authored (tree-sitter-yaml ships none). Mapping
#; keys two levels deep -- a workflow's jobs and each job, a compose file's
#; services and each service -- and no deeper, or the outline would be the
#; file again. A key holding a mapping is a container the breadcrumbs nest
#; under; any other is a field.

(document
  (block_node
    (block_mapping
      (block_mapping_pair
        key: (flow_node) @name
        value: (block_node (block_mapping))) @definition.namespace)))

(document
  (block_node
    (block_mapping
      (block_mapping_pair
        key: (flow_node) @name
        value: [(flow_node) (block_node (block_sequence)) (block_node (block_scalar))]) @definition.field)))

(document
  (block_node
    (block_mapping
      (block_mapping_pair
        value: (block_node
          (block_mapping
            (block_mapping_pair
              key: (flow_node) @name
              value: (block_node (block_mapping))) @definition.namespace))))))

(document
  (block_node
    (block_mapping
      (block_mapping_pair
        value: (block_node
          (block_mapping
            (block_mapping_pair
              key: (flow_node) @name
              value: [(flow_node) (block_node (block_sequence)) (block_node (block_scalar))]) @definition.field))))))
