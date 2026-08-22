#!/usr/bin/env bash
# Visual regression sweep for every bundled theme (theme-polish follow-up,
# Phase 4). Drives a live ned session inside tmux through M-x select-theme
# for each name in kThemeNames below, capturing the resulting pane (with
# SGR escapes, so real colors are visible in the saved output) to
# out-dir/<theme-name>.cap. Each theme is only *previewed*, never
# committed (Enter) -- every entry ends with C-g, which cancels the picker
# and restores whatever theme was active before the sweep started, so this
# never touches the invoking user's own $XDG_STATE_HOME/ned/variables.json.
#
# kThemeNames is a hand-kept copy of ThemeRegistry.cpp's kThemeFactories
# table (the same duplication BundledThemesTest.cpp's own kOriginals/
# kClones lists already accept) -- update it there and here together.
#
# Usage: Tools/theme-sweep.sh [out-dir]   (default out-dir: theme-sweep-out)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

readonly kThemeNames=(
    dark light ansi-dark ansi-light
    major-dark major-light minor-dark minor-light
    high-contrast-dark high-contrast-light mono-dark mono-light fuchsia
    solarized-dark solarized-light gruvbox-dark gruvbox-light nord dracula monokai
    one-dark one-light catppuccin-mocha catppuccin-latte tokyo-night tokyo-night-day
    rose-pine everforest zenburn catppuccin-frappe catppuccin-macchiato tokyo-night-storm
)

readonly kSession="ned-theme-sweep"
readonly kOutDir="${1:-theme-sweep-out}"

cmake --build build -j8 --target ned >/dev/null
mkdir -p "$kOutDir"

tmux kill-session -t "$kSession" 2>/dev/null || true
tmux new-session -d -s "$kSession" -x 160 -y 45 "./build/ned --no-restore"
sleep 1

for name in "${kThemeNames[@]}"; do
    tmux send-keys -t "$kSession" M-x
    tmux send-keys -t "$kSession" "select-theme" Enter
    sleep 0.1
    tmux send-keys -t "$kSession" "$name"
    sleep 0.2
    tmux capture-pane -t "$kSession" -p -e >"$kOutDir/$name.cap"
    tmux send-keys -t "$kSession" C-g
    sleep 0.1
    echo "captured $name -> $kOutDir/$name.cap"
done

tmux send-keys -t "$kSession" C-x C-c
sleep 0.3
tmux kill-session -t "$kSession" 2>/dev/null || true

echo "Done. Review captures under $kOutDir/ (e.g. \`less -R $kOutDir/dracula.cap\`)."
