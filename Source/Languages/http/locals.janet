#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A file variable (`@host = ...`) is the file's own: an environment file
#; defines its own, so renaming one here renames every `{{host}}` it feeds.
#; A body or a request script is read as written -- JSON, XML, GraphQL or
#; JavaScript -- whose `{{host}}` and `client.global.get("host")` the query
#; can't see into, so a name spelled in one declines (`local.opaque`).

((variable_declaration (identifier) @local.definition.var)
 (:set! local.file-private "true"))

(variable (identifier) @local.reference)

(([(raw_body) (json_body) (xml_body) (graphql_body) (pre_request_script) (res_handler_script)]) @local.reference
 (:set! local.opaque "true"))
