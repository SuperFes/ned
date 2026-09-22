#; Symbol-kind query. The grammar ships highlights only. A config's structure
#; is its sections -- <VirtualHost>, <Directory>, <Location>, <IfModule> -- and
#; the name is the tag with what it applies to.

(tag_directive
  name: (tag_name) @name) @definition.module
