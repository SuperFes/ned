# D0 core only -- see ROADMAP.md's parsing-engine section for the deferred
# per-dialect trait-delta work (Postgres/MySQL/SQLite/T-SQL/PL-pgSQL/BigQuery/
# Snowflake). DerekStride/tree-sitter-sql is already a permissive multi-
# dialect grammar (its grammar.json carries dialect-specific rules like
# _postgres_update_statement/_tsql_parameter/dollar_quote directly), which is
# what makes deferring the highlighting-level deltas safe -- the parser
# already accepts the syntax.
#
# No :lsp-root-markers: a .sql file has no fixed project-marker convention
# any more than a .sh script does (see Docs/LanguageSetup.md's bash/cmake
# entries for the same reasoning) -- falls back to editor::ProjectRoot().
#
# Upstream's queries/indents.scm uses nvim-treesitter's @indent.begin/
# @indent.branch/@indent.end convention, not ned's own (@aligned/
# @indent.body/@align.barrier/@indent.suppress, plain @indent/@dedent) --
# not vendored. SQL's parenthesized subqueries/CTEs/column-definition lists
# already fold and indent correctly from the Tier 0 delimiter imprint alone,
# the same as json/css/toml/php (no query source at all).

{:name "sql"
 :extensions [".sql"]
 :line-comment "--"
 :snippets
 {
   "select"
   "SELECT ${1:*}\nFROM ${2:table}\nWHERE ${3:condition};"
   "insert"
   "INSERT INTO ${1:table} (${2:columns})\nVALUES (${3:values});"
   "update"
   "UPDATE ${1:table}\nSET ${2:column} = ${3:value}\nWHERE ${4:condition};"
   "delete"
   "DELETE FROM ${1:table}\nWHERE ${2:condition};"
   "cte"
   "WITH ${1:name} AS (\n    $2\n)\n$0"
   "join"
   "${1:INNER} JOIN ${2:table} ON ${3:condition}"}
}
