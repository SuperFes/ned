# `mix format`, Elixir's canonical formatter: it keeps at most one blank line
# between definitions and adds none. Capture names are format.janet's; keys
# are scoped to elixir automatically. format.janet or ned/set-format-*
# override it, and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
