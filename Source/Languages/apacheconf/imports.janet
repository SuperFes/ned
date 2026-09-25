(simple_directive
  name: (non_quoted_simple_directive_name) @_name
  (simple_param (non_string_param) @import.target)
  (:any-of? @_name "Include" "IncludeOptional")) @import.statement
