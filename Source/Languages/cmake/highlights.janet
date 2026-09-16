# ned-authored (kotlin-tags precedent): a simplified core rather than a
# full transcription of upstream's highlights.scm. The set(... CACHE ...)
# pattern below was dropped for a time -- upstream nests a multi-pattern
# group inside a pattern, a construct QueryMatcher's census-measured scope
# excluded -- see the ROADMAP watch-list entry; restored once the matcher
# gained support (placed after the generic @constant heuristic and the
# "ENV"/"CACHE" @namespace list so its more precise @keyword.modifier/@type
# captures win the overlap on those same tokens -- later patterns override
# on a tie, see Mode.h).

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

(normal_command
  (identifier) @_function
  (:match? @_function "^[sS][eE][tT]$")
  (argument_list
    .
    (argument)
    ((argument) @_cache @keyword.modifier
      .
      (argument) @_type @type
      (:any-of? @_cache "CACHE")
      (:any-of? @_type "BOOL" "FILEPATH" "PATH" "STRING" "INTERNAL"))))

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
