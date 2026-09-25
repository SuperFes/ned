# ned-authored. The imprint indents message, enum, service and oneof bodies;
# an rpc's option block shares its node with the two parenthesized types
# before it, so its braces are stated here.
(rpc "{" @indent.begin "}" @dedent) @indent
