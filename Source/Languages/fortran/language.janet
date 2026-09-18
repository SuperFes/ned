# Free-form Fortran; fixed-form .f/.for files parse as free-form, which
# the grammar tolerates for the common cases.
{:name "fortran"
 :extensions [".f90" ".F90" ".f95" ".F95" ".f03" ".F03" ".f08" ".F08" ".f" ".F" ".for"]
 :line-comment "!"
 :lsp-root-markers ["fpm.toml"]
}
