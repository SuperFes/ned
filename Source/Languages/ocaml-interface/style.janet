# ocamlformat's default (conventional) profile: at most one blank line
# between items. It adds one between multi-line items or items of different
# kinds, which no fixed rule here expresses. Capture names are format.janet's;
# keys are scoped to ocaml-interface automatically. format.janet or ned/set-format-*
# override it, and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
