# apacheconf: imported 2026-09-21 from https://github.com/prigaux/tree-sitter-apacheconf
# (a3c5f64c7afa88b5fb70ae18043e042e3e840113; MIT per package.json, the
# repository ships no LICENSE file). Admission facts
# (Docs/LanguageCoverage.md): generated ABI 15, scanner 0 lines, corpus 6 files.
#
# httpd's own layout has no extension convention -- sites live in
# vhosts.d/*.conf, sites-available/* and conf.d/*.conf -- so only the
# unambiguous basenames are claimed, the same call nginx's definition makes.

{:name "apacheconf"
 :filenames [".htaccess" "httpd.conf" "apache2.conf" "httpd-vhosts.conf" "httpd-ssl.conf"]
 :line-comment "#"
}
