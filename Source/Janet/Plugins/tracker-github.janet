## Bundled GitHub issues provider for ned/tracker-register-provider (see
## Source/Janet/EditorBindings.cpp). Everything goes through `gh`, which owns
## the login and the token: ned stores nothing and never sees a credential.
##
## A connection's :url is the repository, https://github.com/OWNER/REPO (a
## GitHub Enterprise host works the same way). A panel's :query is a GitHub
## issue search handed to `gh issue list --search` verbatim; an empty one
## lists open issues. Keys are "#123", the form a commit message references.
##
## With `gh` installed, :detect adds an issues panel for every github.com
## repository among the project's git remotes -- see
## ned/set-tracker-auto-detect.

(def- github/list-fields "number,title,state,assignees,labels,url,updatedAt")
(def- github/view-fields (string github/list-fields ",body,comments"))

(defn- github/repo-arg
  "gh's --repo form, [HOST/]OWNER/REPO, from a connection's :url."
  [connection]
  (var repo (string/trim (connection :url) " /"))
  (when (empty? repo)
    (error (string "connection \"" (connection :name) "\" needs a :url, https://github.com/OWNER/REPO")))
  (each scheme ["https://" "http://"]
    (when (string/has-prefix? scheme repo)
      (set repo (string/slice repo (length scheme)))))
  (if (string/has-suffix? ".git" repo) (string/slice repo 0 -5) repo))

(defn- github/status [state]
  (case state
    "OPEN" "Open"
    "CLOSED" "Closed"
    (or state "")))

(defn- github/issue [i]
  {:key (string "#" (i :number))
   :title (i :title)
   :status (github/status (i :state))
   :assignee (string/join (map |($ :login) (or (i :assignees) [])) ", ")
   :labels (map |($ :name) (or (i :labels) []))
   :url (i :url)
   :updated (i :updatedAt)})

(defn- github/list-argv [connection query]
  (def argv @["gh" "issue" "list" "--repo" (github/repo-arg connection)
              "--limit" "100" "--json" github/list-fields])
  (unless (empty? query)
    (array/push argv "--search" query))
  argv)

(defn- github/parse-list [output]
  (map github/issue (ned/json-decode output)))

(defn- github/view-argv [connection key]
  ["gh" "issue" "view" (string/trim key "#") "--repo" (github/repo-arg connection)
   "--json" github/view-fields])

(defn- github/number
  "The issue number of key, #42 or 42, for a command that changes the issue."
  [key]
  (def number (string/trim (string/trim key) "#"))
  (unless (and (not (empty? number)) (all |(<= 48 $ 57) number))
    (error (string "\"" key "\" isn't a GitHub issue number")))
  number)

(defn- github/transitions-argv [connection key]
  ["gh" "issue" "view" (github/number key) "--repo" (github/repo-arg connection) "--json" "state"])

(defn- github/parse-transitions [output]
  (if (= ((ned/json-decode output) :state) "OPEN")
    [{:id "close-completed" :name "Close as completed"} {:id "close-not-planned" :name "Close as not planned"}]
    [{:id "reopen" :name "Reopen"}]))

(defn- github/transition-argv [connection key id]
  (def target [(github/number key) "--repo" (github/repo-arg connection)])
  (case id
    "close-completed" ["gh" "issue" "close" ;target "--reason" "completed"]
    "close-not-planned" ["gh" "issue" "close" ;target "--reason" "not planned"]
    "reopen" ["gh" "issue" "reopen" ;target]
    (error (string "unknown GitHub issue transition \"" id "\""))))

(defn- github/assignees-argv [connection key]
  # gh api wants OWNER/REPO in the path, and the host apart.
  (def parts (string/split "/" (github/repo-arg connection)))
  (def [owner repo] (slice parts -3))
  (def host (if (= (length parts) 3) (parts 0) "github.com"))
  ["gh" "api" "--hostname" host "--paginate" "--jq" ".[].login" (string "repos/" owner "/" repo "/assignees")])

(defn- github/parse-assignees [output]
  [{:id "@me" :name "Me"}
   ;(map (fn [login] {:id login :name login})
         (filter |(not (empty? $)) (map string/trim (string/split "\n" output))))])

(defn- github/assign-argv [connection key id]
  ["gh" "issue" "edit" (github/number key) "--repo" (github/repo-arg connection) "--add-assignee" id])

(defn- github/comment-argv [connection key body]
  {:argv ["gh" "issue" "comment" (github/number key) "--repo" (github/repo-arg connection) "--body-file" "{input-file}"]
   :input body})

(defn- github/parse-view [output]
  (def i (ned/json-decode output))
  (merge (github/issue i)
         {:body (i :body)
          :comments (map (fn [c] {:author (get-in c [:author :login])
                                  :created (c :createdAt)
                                  :body (c :body)})
                         (or (i :comments) []))}))

# git@github.com:O/R.git (any user), ssh://git@github.com[:22]/O/R, https://[user@]github.com/O/R.git
(def- github/remote-peg
  (peg/compile
    ~{:host "github.com"
      :userinfo (* (some (if-not (set "@/") 1)) "@")
      :scp (* :userinfo :host ":")
      :url (* (+ "https://" "http://" "ssh://" "git+ssh://" "git://") (? :userinfo) :host (? (* ":" :d+)) "/")
      :main (* (+ :scp :url) (<- (some (if-not "/" 1))) "/" (<- (some (if-not "/" 1))) (? "/") -1)}))

(defn- github/detect [remote-urls]
  (def found @[])
  (def seen @{})
  (when (ned/find-executable "gh")
    (each url remote-urls
      (when-let [[owner raw-repo] (peg/match github/remote-peg url)]
        (def repo (if (string/has-suffix? ".git" raw-repo) (string/slice raw-repo 0 -5) raw-repo))
        (def slug (string owner "/" repo))
        (unless (seen (string/ascii-lower slug))
          (put seen (string/ascii-lower slug) true)
          (array/push found {:name (string "github.com/" slug)
                             :url (string "https://github.com/" slug)
                             :panels [{:name slug :glyph ""}]})))))
  found)

(ned/tracker-register-provider "github"
  {:list-argv github/list-argv
   :parse-list github/parse-list
   :view-argv github/view-argv
   :parse-view github/parse-view
   :detect github/detect
   :numeric-keys true
   :comment-argv github/comment-argv
   :transitions-argv github/transitions-argv
   :parse-transitions github/parse-transitions
   :transition-argv github/transition-argv
   :assignees-argv github/assignees-argv
   :parse-assignees github/parse-assignees
   :assign-argv github/assign-argv
   :mine-query "involves:@me"})
