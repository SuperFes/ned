#; Objective-C's own layer over C's indents query. A message written across
#; lines lines its selector parts up on their colons -- Xcode's and
#; clang-format's layout:
#;
#;     [self doThing:a
#;              with:b];
#;
#; A continuation line that starts anything else, a first line with no colon,
#; or a keyword too long to fit keeps the bracket's ordinary level.

(message_expression) @aligned.colons
