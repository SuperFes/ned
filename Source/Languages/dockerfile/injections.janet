# A shell-form RUN/CMD/ENTRYPOINT runs through /bin/sh -c; the exec form
# (a JSON array) is argv, not shell.
((shell_command) @injection.content
  (:set! injection.language "bash"))
