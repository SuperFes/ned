## Bundled reference implementation of the ned/vcs-register-provider vocabulary
## (see Source/Janet/EditorBindings.cpp) for git. Loaded automatically by
## LoadBundledPlugins (Source/Janet/InitFile.cpp) before a user's own
## init.janet runs, so it can be overridden/unregistered there if wanted.
##
## Intended as the annotated example for anyone writing a plugin for another
## VCS (Mercurial, jj, ...): the shape to copy is "detect a repo, build an
## argv, parse the captured output into an array of tables" -- ned itself
## runs the subprocess, this file never shells out directly.

(defn detect [root]
  (not (nil? (os/stat (string root "/.git")))))

# ned itself never chdirs before spawning a plugin's argv -- the subprocess
# inherits ned's own working directory, which has nothing to do with
# whichever file's blame/log was requested. Every argv below starts with
# "-C" pointed at path's own containing directory so git resolves the
# repository (and the path itself) relative to the right place, not
# wherever ned happened to be launched from.
(defn- dirname [path]
  (def slashes (string/find-all "/" path))
  (if (empty? slashes) "." (string/slice path 0 (last slashes))))

(defn blame-argv [path]
  ["git" "-C" (dirname path) "blame" "--porcelain" path])

(defn log-argv [path]
  ["git" "-C" (dirname path) "log" "--follow" "--date=short"
   (string "--pretty=format:%H" "\x1f" "%an" "\x1f" "%ad" "\x1f" "%s")
   path])

# -U0: no context lines, only the changed ones themselves -- exactly what a
# gutter marker needs (which lines changed, not their surrounding context),
# and what every git-gutter-style plugin in other editors asks for too.
(defn diff-argv [path]
  ["git" "-C" (dirname path) "diff" "--no-color" "-U0" "--" path])

## --- multibuffers: full working-tree diff ---------------------------------
##
## Unlike diff-argv above (single file, -U0 for the gutter), this is
## root-scoped (every changed file in one call) and keeps git's normal
## default context (3 lines) -- meant to be read in a *vcs diff* multibuffer,
## not just measured for gutter markers.

(defn working-diff-argv [root]
  ["git" "-C" root "diff" "--no-color"])

## --- status/stage/unstage/commit/branch (vocabulary-completion) ----------
##
## The root-scoped operations get the project root as their argument (see
## VcsRunner's own root-scoped requests); stage/unstage get the target
## file's path, same as blame/log/diff.

(defn status-argv [root]
  ["git" "-C" root "status" "--porcelain"])

(defn stage-argv [path]
  ["git" "-C" (dirname path) "add" "--" path])

# reset -q HEAD --, not `git restore --staged`: identical effect, but reset
# predates git 2.23 by a decade -- the more portable spelling of the same
# thing. Fails with git's own clear message in a repository with no commits
# yet (no HEAD to reset to); surfaced verbatim, not special-cased here.
(defn unstage-argv [path]
  ["git" "-C" (dirname path) "reset" "-q" "HEAD" "--" path])

## --- hunk-level staging (hunk-staging follow-up) --------------------------
##
## staged-diff is diff's index-vs-HEAD counterpart -- the diff an unstage
## selects its hunk from. The patch itself is built by ned (a verbatim
## slice of the raw diff output, see Editor/Vcs/DiffPatch.h) and handed
## over as a file path; --unidiff-zero is required because these are -U0
## (zero-context) patches, which `git apply` otherwise rejects outright as
## a guard against context-free misapplication.

(defn staged-diff-argv [path]
  ["git" "-C" (dirname path) "diff" "--cached" "--no-color" "-U0" "--" path])

(defn stage-patch-argv [root patch-path]
  ["git" "-C" root "apply" "--cached" "--unidiff-zero" patch-path])

(defn unstage-patch-argv [root patch-path]
  ["git" "-C" root "apply" "--cached" "--reverse" "--unidiff-zero" patch-path])

(defn commit-argv [root message]
  ["git" "-C" root "commit" "-m" message])

(defn branch-list-argv [root]
  ["git" "-C" root "branch" "--list" "--no-color"])

# checkout, not `git switch`, for the same pre-2.23 portability reason as
# unstage-argv above (switch is also still marked experimental in its own
# man page).
(defn branch-switch-argv [root name]
  ["git" "-C" root "checkout" name])

(defn branch-create-argv [root name]
  ["git" "-C" root "checkout" "-b" name])

## --- VCS side panel: revert/stash/push-pull-fetch/ahead-behind ------------
##
## revert-argv targets HEAD explicitly (not `git checkout -- path`, which
## would restore from the index instead of HEAD for an already-staged file)
## -- "revert this file's working tree back to HEAD" is meant to discard
## both staged and unstaged local changes alike, distinct from unstage-argv
## above (index only). Push/pull/fetch rely on an already-configured
## upstream tracking branch -- no remote/branch arguments, a documented v1
## cut (see ROADMAP.md).

(defn revert-argv [path]
  ["git" "-C" (dirname path) "checkout" "HEAD" "--" path])

(defn stash-list-argv [root]
  ["git" "-C" root "stash" "list" "--pretty=format:%gd\t%s"])

(defn stash-push-argv [root message]
  (if (empty? message)
    ["git" "-C" root "stash" "push"]
    ["git" "-C" root "stash" "push" "-m" message]))

(defn stash-pop-argv [root stash-ref]
  ["git" "-C" root "stash" "pop" stash-ref])

(defn stash-drop-argv [root stash-ref]
  ["git" "-C" root "stash" "drop" stash-ref])

(defn push-argv [root]
  ["git" "-C" root "push"])

(defn pull-argv [root]
  ["git" "-C" root "pull"])

(defn fetch-argv [root]
  ["git" "-C" root "fetch"])

# HEAD...@{u} (triple-dot symmetric-difference) against the upstream --
# fails with git's own clear error when no upstream is configured, surfaced
# verbatim through the same RequestAheadBehind onError path every other
# unsupported/failing operation already uses.
(defn ahead-behind-argv [root]
  ["git" "-C" root "rev-list" "--left-right" "--count" "HEAD...@{u}"])

## --- blame porcelain parsing -------------------------------------------
##
## `git blame --porcelain` emits, per source line, either a full commit-info
## block (the first time a given commit is seen) or just a short repeat
## header (every later line attributed to the same commit) followed by the
## actual "\t<line content>" line. commits caches each hash's fields the
## first time they're seen so later repeats can look them up instead of
## re-parsing.

(defn- commit-field [commits hash field]
  (def c (get commits hash))
  (if c (get c field "") ""))

(defn- known-prefix? [line]
  (or (string/has-prefix? "author" line)
      (string/has-prefix? "committer" line)
      (string/has-prefix? "summary " line)
      (string/has-prefix? "previous " line)
      (string/has-prefix? "filename " line)
      (= line "boundary")))

(defn parse-blame [stdout]
  (def commits @{})
  (def result @[])
  (var current-hash "")
  (each line (string/split "\n" stdout)
    (cond
      (string/has-prefix? "\t" line)
      (array/push result
        {:hash current-hash
         :author (commit-field commits current-hash :author)
         :date (commit-field commits current-hash :date)
         :summary (commit-field commits current-hash :summary)})

      (string/has-prefix? "author " line)
      (put (get commits current-hash) :author (string/slice line 7))

      (string/has-prefix? "author-time " line)
      (put (get commits current-hash) :date (string/slice line 12))

      (string/has-prefix? "summary " line)
      (put (get commits current-hash) :summary (string/slice line 8))

      (known-prefix? line)
      nil

      # The one remaining shape is a header line: "<40-hex-hash> <orig-line>
      # <final-line> [<num-lines>]". Anything this short and unmatched above
      # is blank/malformed input, not a real header -- skipped rather than
      # crashing the parse.
      (>= (length line) 40)
      (do
        (def hash (string/slice line 0 40))
        (unless (get commits hash)
          (put commits hash @{:author "" :date "" :summary ""}))
        (set current-hash hash))

      true nil))
  result)

## --- log parsing ---------------------------------------------------------
##
## One line per commit, fields separated by the \x1f unit-separator byte
## (chosen by log-argv's own --pretty=format so a commit summary containing
## a literal comma/pipe/tab can't be mistaken for a field boundary).

(defn parse-log [stdout]
  (def result @[])
  (each line (string/split "\n" stdout)
    (unless (empty? line)
      (def parts (string/split "\x1f" line))
      (array/push result
        {:hash (get parts 0 "")
         :author (get parts 1 "")
         :date (get parts 2 "")
         :summary (get parts 3 "")})))
  result)

## --- diff hunk-header parsing ---------------------------------------------
##
## `git diff -U0` emits one "@@ -oldStart[,oldCount] +newStart[,newCount] @@"
## header per changed hunk (count omitted when it's 1), often followed by
## trailing context text (the enclosing function name, if git found one) on
## the same line after the closing "@@" -- parse-range/parse-hunk-header
## only ever look at the two range fields between the "@@ -"/" @@" markers,
## so that trailing text is naturally ignored.

(defn- parse-range [text]
  (def comma (string/find "," text))
  (if comma
    [(scan-number (string/slice text 0 comma)) (scan-number (string/slice text (+ comma 1)))]
    [(scan-number text) 1]))

(defn- parse-hunk-header [line]
  (def end (string/find " @@" line 3))
  (def middle (string/slice line 3 end))
  (def parts (string/split " " middle))
  (def old-range (parse-range (string/slice (get parts 0) 1))) # drop leading "-"
  (def new-range (parse-range (string/slice (get parts 1) 1))) # drop leading "+"
  {:old-start (get old-range 0) :old-count (get old-range 1)
   :new-start (get new-range 0) :new-count (get new-range 1)})

(defn parse-diff [stdout]
  (def result @[])
  (each line (string/split "\n" stdout)
    (when (string/has-prefix? "@@ " line)
      (array/push result (parse-hunk-header line))))
  result)

## --- status parsing -------------------------------------------------------
##
## `git status --porcelain` emits one "XY <path>" line per changed/untracked
## file: two state letters (index then worktree -- "??" for untracked), one
## space, then the path relative to the repository root. A staged rename is
## "R  old -> new"; the new name is the one every downstream consumer
## (staging, visiting) wants. git double-quotes paths containing special
## characters (C-style escapes inside) -- the surrounding quotes are
## stripped so the common "path with a space" case works, but the inner
## escapes are left as-is, a recorded degrade-don't-crash simplification
## for genuinely exotic filenames (embedded newlines/quotes).

(defn- unquote-path [path]
  (if (and (string/has-prefix? "\"" path) (string/has-suffix? "\"" path) (> (length path) 1))
    (string/slice path 1 (- (length path) 1))
    path))

(defn parse-status [stdout]
  (def result @[])
  (each line (string/split "\n" stdout)
    (when (>= (length line) 4)
      (def state (string/slice line 0 2))
      (var path (string/slice line 3))
      (def arrow (string/find " -> " path))
      (when arrow
        (set path (string/slice path (+ arrow 4))))
      (array/push result {:state state :path (unquote-path path)})))
  result)

## --- branch-list parsing --------------------------------------------------
##
## `git branch --list` emits one branch per line: two marker columns ("* "
## current, "+ " checked out in another worktree, "  " otherwise) then the
## name. A detached HEAD shows as "* (HEAD detached at ...)" -- a
## parenthetical placeholder, not a real branch, skipped so it can't be
## offered as a switch target.

(defn parse-branch-list [stdout]
  (def result @[])
  (each line (string/split "\n" stdout)
    (when (>= (length line) 3)
      (def name (string/slice line 2))
      (unless (string/has-prefix? "(" name)
        (array/push result {:name name :current (string/has-prefix? "* " line)}))))
  result)

## --- stash-list parsing ----------------------------------------------------
##
## One "<ref>\t<message>" line per stash entry (stash-list-argv's own
## --pretty=format), tab-separated the same way ahead-behind's rev-list
## output below is -- one consistent separator convention across both new
## parsers.

(defn parse-stash-list [stdout]
  (def result @[])
  (each line (string/split "\n" stdout)
    (unless (empty? line)
      (def parts (string/split "\t" line))
      (array/push result {:ref (get parts 0 "") :message (get parts 1 "")})))
  result)

## --- ahead/behind parsing ---------------------------------------------------
##
## `git rev-list --left-right --count HEAD...@{u}` emits one line,
## "<ahead>\t<behind>" (HEAD is the left side of the symmetric difference).

(defn parse-ahead-behind [stdout]
  (def parts (string/split "\t" (string/trim stdout)))
  {:ahead (scan-number (get parts 0 "0")) :behind (scan-number (get parts 1 "0"))})

(ned/vcs-register-provider "git"
  {:detect detect
   :blame-argv blame-argv
   :parse-blame parse-blame
   :log-argv log-argv
   :parse-log parse-log
   :diff-argv diff-argv
   :parse-diff parse-diff
   :working-diff-argv working-diff-argv
   :status-argv status-argv
   :parse-status parse-status
   :stage-argv stage-argv
   :unstage-argv unstage-argv
   :staged-diff-argv staged-diff-argv
   :stage-patch-argv stage-patch-argv
   :unstage-patch-argv unstage-patch-argv
   :commit-argv commit-argv
   :branch-list-argv branch-list-argv
   :parse-branch-list parse-branch-list
   :branch-switch-argv branch-switch-argv
   :branch-create-argv branch-create-argv
   :revert-argv revert-argv
   :stash-list-argv stash-list-argv
   :parse-stash-list parse-stash-list
   :stash-push-argv stash-push-argv
   :stash-pop-argv stash-pop-argv
   :stash-drop-argv stash-drop-argv
   :push-argv push-argv
   :pull-argv pull-argv
   :fetch-argv fetch-argv
   :ahead-behind-argv ahead-behind-argv
   :parse-ahead-behind parse-ahead-behind})
