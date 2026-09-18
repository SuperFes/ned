#; Symbol-kind query, ned-authored. Sectioning is the outline of a LaTeX
#; document; a \label is the name other places refer to.
(part
  text: (curly_group
    (text) @name)) @definition.module
(chapter
  text: (curly_group
    (text) @name)) @definition.module
(section
  text: (curly_group
    (text) @name)) @definition.module
(subsection
  text: (curly_group
    (text) @name)) @definition.module
(subsubsection
  text: (curly_group
    (text) @name)) @definition.module

(new_command_definition
  declaration: (curly_group_command_name
    (command_name) @name)) @definition.function
(old_command_definition
  declaration: (command_name) @name) @definition.function
(environment_definition
  name: (curly_group_text
    (text) @name)) @definition.function

(label_definition
  name: (curly_group_label
    (label) @name)) @definition.constant
(label_reference
  names: (curly_group_label_list
    (label) @name)) @reference.constant
