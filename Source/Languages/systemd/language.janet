# systemd: imported 2026-09-21 from https://github.com/adamrunner/tree-sitter-systemd
# (e92ff198aa8ac6f8e8975c457acc06596211253b; MIT per package.json, the
# repository ships no LICENSE file). Admission facts
# (Docs/LanguageCoverage.md): generated ABI 15, scanner 0 lines, corpus 4
# files upstream plus ned's own unit_syntax.txt.
#
# Unit, networkd and nspawn files all share this syntax. The manager's own
# configuration is *.conf: the distinctive basenames are claimed, while
# "system.conf" and "user.conf" are too generic to take from whoever else
# ships one.

{:name "systemd"
 :extensions [".service" ".socket" ".timer" ".target" ".mount" ".automount" ".swap" ".path" ".device" ".scope" ".slice"
              ".network" ".netdev" ".link" ".nspawn" ".dnssd"]
 :filenames ["journald.conf" "logind.conf" "resolved.conf" "timesyncd.conf" "networkd.conf" "coredump.conf"
             "homed.conf" "oomd.conf" "sleep.conf" "pstore.conf" "journal-remote.conf" "journal-upload.conf"]
 :line-comment "#"
}
