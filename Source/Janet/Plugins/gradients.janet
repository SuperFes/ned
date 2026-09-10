# Bundled paints: the array sugar, and the preset set every theme can build
# from. Loaded before init.janet, so redefining any name here simply wins --
# the same bundled-then-user-overrides order vcs-git.janet already relies on.
#
# The C++ side parses exactly one thing: the one-line form
# ("y $bg 3 $bg+8"). Everything below is sugar that flattens an array into
# that string, which is what keeps the whole grammar out of the binding
# layer -- when the scripting language changes, this file is rewritten and
# the parser is not.

(defn ned/paint-spec
  "Flatten a paint into its one-line form. A string passes through; an array
  becomes `axis-or-pattern stop weight stop ...` with keywords stripped of
  their colon and numbers rendered as weights."
  [paint]
  (if (string? paint)
    paint
    (string/join
      (map (fn [part]
             (cond
               (keyword? part) (string part)
               (number? part)  (string part)
               (string? part)  part
               (string part)))
           paint)
      " ")))

(defn ned/gradient
  "Register a named paint from an array or a string:
    (ned/gradient \"brand\" [:diag \"$accent\" 2 \"$keyword\"])"
  [name paint]
  (ned/theme-gradient name (ned/paint-spec paint)))

(defn ned/surface
  "Set one or more parts of a surface at once:
    (ned/surface \"popup\" :fill [:y \"$bg/78\" 3 \"$bg/52\"] :border \"$brand\")"
  [name & parts]
  (var i 0)
  (while (< i (length parts))
    (let [part (get parts i)
          spec (get parts (+ i 1))]
      (ned/theme-surface name (string part) (ned/paint-spec spec)))
    (set i (+ i 2))))

# --- palette-derived presets --------------------------------------------
# These resolve against the active theme's own slots, so they follow a theme
# instead of fighting it. They are what the derived surfaces should be built
# from once the widgets migrate.

(ned/gradient "lift"   [:y "$bg" "$bg+6"])           # panel fill, barely there
(ned/gradient "sink"   [:y "$bg-5" "$bg+3"])         # inset wells, input rows
(ned/gradient "glass"  [:y "$bg/78" 3 "$bg/52"])     # popup body
(ned/gradient "edge"   [:x "$bg/0" 6 "$bg/65"])      # dock-edge falloff
(ned/gradient "scrim"  "$bg/55")                     # focus dimmer
(ned/gradient "focus"  [:x "$accent/32" 4 "$accent/0"])  # current-line wash
(ned/gradient "brand"  [:diag "$accent" 2 "$keyword"])   # tabs, popup borders
(ned/gradient "rule"   [:x "$subtle/0" "$subtle" "$subtle/0"])  # separator, fades both ends

# --- signature ramps ----------------------------------------------------
# Absolute colours, deliberately loud, opt-in. `spectrum` is the four-stop
# hue path from Tools/NotcursesGradientProbe.cpp, kept byte for byte.

(ned/gradient "spectrum" [:x "#2859dc" "#28c8be" "#e6be3c" "#e13c6e"])
(ned/gradient "aurora"   [:x "#0b3d54" "#17c3a2" 2 "#7c5cff"])
(ned/gradient "sunset"   [:x "#ff8a3c" "#ff3c7d" 2 "#6a2c9c"])
(ned/gradient "ember"    [:x "#2a0a0a" 2 "#b3341c" "#ffb238"])
(ned/gradient "ice"      [:x "#0b2545" 2 "#4cc9f0" "#e0fbfc"])
(ned/gradient "vapor"    [:x "#ff71ce" "#01cdfe" "#05ffa1"])
(ned/gradient "ink"      [:y "#0d0d12" "#4a4a58"])

# --- fades, for the text layer ------------------------------------------
# Absolute colour is the wrong tool over code: a fade keeps the syntax
# colour and takes some of it away.

(ned/gradient "vanish" [:x "100%" "0%"])   # trail off toward an edge
(ned/gradient "ghost"  "45%")              # present but receded

# --- patterns -----------------------------------------------------------
# Patterns texture a surface's *empty* space: they decline cells carrying a
# glyph, because a checkerboard behind code is unreadable.

(ned/gradient "grain"     [:noise 8 "$bg"])
(ned/gradient "scanlines" [:scanlines 2 "$bg/0" "$bg-8"])
(ned/gradient "checker"   [:checker 1 "$bg/0" "$subtle/22"])
(ned/gradient "hatch"     [:hatch 3 "$bg/0" "$subtle/30"])
(ned/gradient "graph"     [:grid 8 "$bg/0" "$subtle/18"])
(ned/gradient "stipple"   [:dots 3 "$bg/0" "$subtle/28"])
