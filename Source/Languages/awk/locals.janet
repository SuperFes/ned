#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; A function's parameters are its only locals -- by convention the extra
#; ones after a gap are its scratch variables. Everything else is global.

(func_def) @local.scope

(func_def (param_list (identifier) @local.definition.parameter))

(identifier) @local.reference

(func_def name: (identifier) @local.skip)
