(section
  (section_header (section_name) @_section)
  (variable (name) @_name value: (string) @import.target)
  (:any-of? @_section "include" "includeIf")
  (:eq? @_name "path"))
