#; Symbol-kind query, ned-authored (tree-sitter-kdl ships none). Nodes two
#; levels deep, like YAML's keys: a node with children is a container the
#; breadcrumbs nest under; any other is a field.

(document
  (node
    name: (identifier) @name
    children: (node_children)) @definition.namespace)

(document
  (node
    name: (identifier) @name
    !children) @definition.field)

(document
  (node
    children: (node_children
      (node
        name: (identifier) @name
        children: (node_children)) @definition.namespace)))

(document
  (node
    children: (node_children
      (node
        name: (identifier) @name
        !children) @definition.field)))
