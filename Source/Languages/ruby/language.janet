{:name "ruby"
 :extensions [".rb" ".rake" ".gemspec"]
 :injection-aliases ["rb"]
 :filenames ["Rakefile" "Gemfile" "Guardfile"]
 :line-comment "#"
 :lsp-root-markers ["Gemfile"]
 :line-continuation "\\"
 :signature-template "def __ned_sig({})\nend"
 :import-resolution {:extensions ["rb"] :source-roots ["lib"]}
 :queries {:locals ["ruby/locals.janet"]}
}
