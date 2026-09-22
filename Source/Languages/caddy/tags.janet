#; Symbol-kind query. The grammar ships highlights, injections and locals. A
#; Caddyfile's structure is its site blocks and the snippets/named routes they
#; reuse.

(snippet_definition
  name: (snippet_name) @name) @definition.module

(named_route
  name: (named_route_identifier) @name) @definition.module
