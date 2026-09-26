# Meson has no extension of its own: a build file is meson.build, its
# options file meson.options (meson_options.txt before 1.1).
{:name "meson"
 :filenames ["meson.build" "meson.options" "meson_options.txt"]
 :line-comment "#"
 :not-applicable {:continuation "an operator can't end a line (`a +` then a newline is invalid)"
                  :locals       "variables are shared across subdir() files"
                  :tags         "no definitions: targets are assignments"
                  :injections   "nothing in it is written in another language"
                  :signatures   "no user-defined functions"
                  :format       "no definitions or delimited bodies for the formatter's rules to place"}
}
