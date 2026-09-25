(use_statement module: (package) @import.module) @import.statement
(require_expression (bareword) @import.module) @import.statement
(require_expression (interpolated_string_literal content: (string_content) @import.target)) @import.statement
(require_expression (string_literal content: (string_content) @import.target)) @import.statement
