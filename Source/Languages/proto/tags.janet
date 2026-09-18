#; Symbol-kind query, ned-authored: coder3101/tree-sitter-proto ships no
#; tags. Messages, enums and services are the types; rpcs the callables.
(message
  (message_name) @name) @definition.class
(enum
  (enum_name) @name) @definition.enum
(service
  (service_name) @name) @definition.interface
(rpc
  (rpc_name) @name) @definition.method
(field
  (identifier) @name) @definition.field
(package
  (full_ident) @name) @definition.module
