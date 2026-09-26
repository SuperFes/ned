#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; A Terraform/OpenTofu test file's top-level `run "name" { ... }` blocks
#; (`.tftest.hcl` / `.tofutest.hcl`, which claim this mode by their `.hcl`
#; extension).

((config_file
   (body
     (block
       (identifier) @_kind
       .
       (string_lit (template_literal) @test.name)) @test.definition))
 (:eq? @_kind "run"))
