# ned-authored (kotlin-tags precedent): upstream's highlights.scm nests a
# multi-pattern group inside a pattern, which QueryMatcher's census-measured
# scope deliberately excludes -- see the ROADMAP watch-list entry. This file
# keeps the useful core in constructs the matcher supports.

[
  (quoted_argument)
  (bracket_argument)
] @string

(escape_sequence) @string.escape

(variable) @variable

[
  (bracket_comment)
  (line_comment)
] @comment

(normal_command
  (identifier) @function.call)

((normal_command
  (identifier) @function.builtin)
  (:match? @function.builtin "^(?i)(set|unset|list|string|math|file|message|include|option|project|return|separate_arguments|cmake_minimum_required|cmake_parse_arguments|cmake_policy|find_package|find_library|find_path|find_program|add_library|add_executable|add_subdirectory|add_dependencies|add_custom_command|add_custom_target|add_compile_options|add_compile_definitions|add_link_options|target_link_libraries|target_include_directories|target_compile_definitions|target_compile_features|target_compile_options|target_link_options|target_sources|set_target_properties|set_property|get_property|get_target_property|install|configure_file|enable_testing|add_test|execute_process)$"))

((argument
  (unquoted_argument) @constant)
  (:match? @constant "^[A-Z@][A-Z0-9_]+$"))

[
  "ENV"
  "CACHE"
] @namespace

[
  "$"
  "{"
  "}"
] @punctuation.special

(function_command
  (argument_list
    (argument) @function))

(macro_command
  (argument_list
    (argument) @function))

[
  (function)
  (endfunction)
  (macro)
  (endmacro)
  (block)
  (endblock)
] @keyword.function

[
  (if)
  (elseif)
  (else)
  (endif)
] @keyword.control.conditional

[
  (foreach)
  (endforeach)
  (while)
  (endwhile)
] @keyword.control.repeat
