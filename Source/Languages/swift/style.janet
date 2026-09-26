# swift-format's defaults (Apple's formatter, shipped with the toolchain):
# K&R braces, `} else {` on one line (lineBreakBeforeControlFlowKeywords
# false), and at most one blank line (maximumBlankLines 1). Capture names
# are format.janet's; keys are scoped to swift automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:break {"brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "control.keyword" {:before false}}
 :blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
