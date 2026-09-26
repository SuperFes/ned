(message (message_body) @brace.class)
(enum (enum_body) @brace.class)
(oneof "{" @brace.class.open "}" @brace.class.close)
(service "{" @brace.interface.open "}" @brace.interface.close)

(source_file [(message) (enum) (service) (extend)] @def.toplevel)
(source_file . [(message) (enum) (service) (extend)] @def.toplevel.first)
