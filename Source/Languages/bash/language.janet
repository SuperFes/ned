{:name "bash"
 # .ebuild/.eclass are Gentoo Portage's build-script format -- both are
 # genuinely bash (sourced by portage's own bash-based build system: EAPI,
 # inherit, src_prepare()/src_configure()/... function definitions, [[ ]]
 # tests, arrays), so the bundled bash grammar parses them correctly with no
 # dedicated grammar of its own. make.conf (Portage's system-wide build
 # config, plain VAR="value" assignments) is the same story.
 :extensions [".sh" ".bash" ".ebuild" ".eclass"]
 :filenames ["make.conf"]
 :line-comment "#"
 :import-resolution {:extensions ["sh"]}
 :injection-aliases ["sh" "shell" "zsh"]
 :snippets
 {
   "shebang"
   "#!/usr/bin/env bash\nset -euo pipefail\n\n$0"
   "for"
   "for ${1:item} in ${2:list}; do\n    $0\ndone"
   "while"
   "while ${1:condition}; do\n    $0\ndone"
   "if"
   "if ${1:[ condition ]}; then\n    $0\nfi"
   "ifelse"
   "if ${1:[ condition ]}; then\n    $2\nelse\n    $0\nfi"
   "fn"
   "${1:name}() {\n    $0\n}"}
}
