# ned-authored. Every body -- a target's, IF/ELSE's, FOR's, TRY's -- is a
# `block` whose lines sit one level in; ELSE/FINALLY/END are outside it. The
# imprint can't see these: the scanner's `_indent` token opens a target's.
(block) @indent
# WITH DOCKER holds exactly one RUN, not a block.
(with_docker_command) @indent
(with_docker_command "END" @dedent)

#; A target with no body yet: the line after its header opens one.
((target . (identifier) .) @indent.headed)
#; A block construct whose END isn't written yet parses as an error holding
#; its keyword; its body opens on the next line all the same.
((ERROR ["IF" "FOR" "TRY" "WAIT" "WITH DOCKER"]) @indent.headed)
