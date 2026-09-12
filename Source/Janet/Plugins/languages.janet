# Bundled capture classifiers -- the language escapes that need a real
# language rather than a query pattern (Docs/ParsingEngine.md's Tier 2):
# arithmetic over captured text, and comparison against state configured at
# runtime. Loaded by PluginLoader before the user's init.janet, so a
# (ned/register-capture-classifier ...) there replaces one of these
# outright -- re-registering overwrites, the registry convention.

# Org headline level: a cyclic function of the counted stars (three curated
# level faces, real Org's eight cycled down -- SyntaxClass::HeadlineLevel1's
# own doc comment). The capture is the stars themselves; the definition's
# :capture-spans widens the classified span through the headline's line.
(ned/register-capture-classifier "org" "org.headline.stars"
  (fn [stars]
    (case (% (dec (length stars)) 3)
      0 :headline-level-1
      1 :headline-level-2
      :headline-level-3)))

# Org TODO-vs-DONE: an exact match against the runtime-configured keyword
# list (ned/org-todo-keywords -- ned/set-org-todo-keywords is the setter),
# the LAST keyword reading as the done state -- the standard single-sequence
# Org convention. A first word that is not a configured keyword suppresses
# the span entirely (false): the headline wash underneath must show through,
# not a default-classed span over it.
(ned/register-capture-classifier "org" "org.keyword.candidate"
  (fn [text]
    (def keywords (ned/org-todo-keywords))
    (def idx (find-index |(= $ text) keywords))
    (cond
      (nil? idx) false
      (= idx (dec (length keywords))) :done-keyword
      :todo-keyword)))
