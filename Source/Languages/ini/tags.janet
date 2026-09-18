#; Symbol-kind query. justinmk/tree-sitter-ini ships highlights only; a
#; section is the one thing worth a breadcrumb in an INI file, and it reads
#; as a namespace over its settings.
(section
  (section_name
    (text) @name)) @definition.module
