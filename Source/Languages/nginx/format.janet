# A block-carrying directive; a bare `user nginx;` line isn't a definition.
(source_file [(directive (block)) (attribute (block))] @def.toplevel)
(source_file . [(directive (block)) (attribute (block))] @def.toplevel.first)
