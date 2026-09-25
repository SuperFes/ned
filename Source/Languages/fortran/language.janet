# Free-form Fortran; fixed-form .f/.for files parse as free-form, which
# the grammar tolerates for the common cases.
{:name "fortran"
 :extensions [".f90" ".F90" ".f95" ".F95" ".f03" ".F03" ".f08" ".F08" ".f" ".F" ".for"]
 :injection-aliases ["f90"]
 :line-comment "!"
 :lsp-root-markers ["fpm.toml"]
 :capture-classes {"custom_directive" :keyword}
 :import-resolution {:extensions ["inc" "f90" "f"]}
 :signature-template "subroutine ned_sig({})\nend subroutine"
}
