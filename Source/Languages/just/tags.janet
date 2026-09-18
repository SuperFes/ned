#; Symbol-kind query. IndianBoy42/tree-sitter-just ships highlights,
#; injections and locals but no tags. A recipe is the callable unit; a
#; top-level assignment, an alias and a module are the file's other names.
(recipe
  (recipe_header
    name: (identifier) @name)) @definition.function

(alias
  left: (identifier) @name) @definition.function

(assignment
  left: (identifier) @name) @definition.var

(module
  name: (identifier) @name) @definition.module
