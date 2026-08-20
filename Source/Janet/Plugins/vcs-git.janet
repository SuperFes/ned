## Bundled reference implementation of the ned/vcs-register-provider vocabulary
## (see Source/Janet/EditorBindings.cpp) for git. Loaded automatically by
## LoadBundledPlugins (Source/Janet/InitFile.cpp) before a user's own
## init.janet runs, so it can be overridden/unregistered there if wanted.
##
## Intended as the annotated example for anyone writing a plugin for another
## VCS (Mercurial, jj, ...): the shape to copy is "detect a repo, build an
## argv, parse the captured output into an array of {:hash :author :date
## :summary} entries" -- ned itself runs the subprocess, this file never
## shells out directly.

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

(ned/vcs-register-provider "git" detect blame-argv parse-blame log-argv parse-log)
