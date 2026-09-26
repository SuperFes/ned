# The protobuf style guide is Google's, which clang-format's Google style
# applies to .proto files: braces on the header's line and at most one
# blank line in a row.
{:break {"brace.class"     {:placement :same-line}
         "brace.interface" {:placement :same-line}}
 :blank {"def.toplevel" {:max-before 1}}}
