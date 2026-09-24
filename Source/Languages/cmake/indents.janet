# ned-authored. Every block command (if/foreach/while/function/macro/block)
# holds its commands in a `body` node between its opening and closing
# commands; the elseif/else/end* commands are the body's siblings, so they
# stay at the construct's own level.
(body) @indent
