# Format captures (see Docs/FormattingRules.md for each name's pass).
# Definitions are calls: a module at the top, its functions and macros in its
# do block.

((source (call target: (identifier) @_head) @def.toplevel)
 (:any-of? @_head "defmodule" "defprotocol" "defimpl"))
((source . (call target: (identifier) @_head) @def.toplevel.first)
 (:any-of? @_head "defmodule" "defprotocol" "defimpl"))

((do_block (call target: (identifier) @_head) @def.method)
 (:any-of? @_head "def" "defp" "defmacro" "defmacrop" "defguard" "defguardp" "defdelegate"))
((do_block . (call target: (identifier) @_head) @def.method.first)
 (:any-of? @_head "def" "defp" "defmacro" "defmacrop" "defguard" "defguardp" "defdelegate"))
