# ned-authored. Every body -- a target's, IF/ELSE's, FOR's, TRY's -- is a
# `block` whose lines sit one level in; ELSE/FINALLY/END are outside it. The
# imprint can't see these: the scanner's `_indent` token opens a target's.
(block) @indent
# WITH DOCKER holds exactly one RUN, not a block.
(with_docker_command) @indent
(with_docker_command "END" @dedent)
