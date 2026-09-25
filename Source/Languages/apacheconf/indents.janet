# ned-authored. A `<Directory>`/`<VirtualHost>`/`<IfModule>` section is a
# named node rather than a delimiter pair, so the imprint sees no body: its
# directives go one level in and its `</...>` line back to the opener's.
(tag_directive) @indent
(tag_directive "</" @dedent)
