# rustfmt's defaults (https://rust-lang.github.io/rustfmt/), Rust's canonical
# style. Capture names are format.janet's; keys are scoped to rust
# automatically. A project's rustfmt.toml adjusts what rustfmt lets it
# adjust; format.janet or ned/set-format-* override either, and
# (ned/set-format-builtin-style false) turns this file off.
#
# Indentation (4 spaces, hard_tabs = false) comes from IndentDefaults.
{:break {# brace_style = "SameLineWhere": an item's `{` stays on its line...
         "brace.function"        {:placement :same-line}
         "brace.class"           {:placement :same-line}
         "brace.interface"       {:placement :same-line}
         "brace.namespace"       {:placement :same-line}
         # ...unless a where clause precedes it
         "brace.function.where"  {:placement :next-line}
         "brace.class.where"     {:placement :next-line}
         "brace.interface.where" {:placement :next-line}
         # control_brace_style = "AlwaysSameLine": `if x {`, `} else {`
         "brace.control"         {:placement :same-line}
         "control.keyword"       {:before false}}
 # blank_lines_upper_bound = 1
 :blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
