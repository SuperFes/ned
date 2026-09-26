#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; An anchor (`&defaults`) is the binding and an alias (`*defaults`) a use of
#; it, within one document of the stream. YAML has no includes, so an anchor
#; is private to its file even when its document is the whole file. An alias
#; names the latest anchor before it; a redefined anchor is one binding here,
#; which renames both spellings together rather than splitting them.

(document) @local.scope

((anchor (anchor_name) @local.definition.anchor)
 (:set! local.file-private "true"))

(alias (alias_name) @local.reference)
