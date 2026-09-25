#; ned's delta over the vendored upstream/locals.janet.

#; A `let` binds after its value: `let x = x + 1` reads the x before it.
(let
  value: (_) @local.initializer) @local.declaration
