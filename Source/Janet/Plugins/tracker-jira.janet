## Bundled Jira Cloud provider for ned/tracker-register-provider (see
## Source/Janet/EditorBindings.cpp), over Jira's REST API with curl.
##
## A connection's :url is the site, https://SITE.atlassian.net, and its
## :email and :token-command (printing an API token from
## https://id.atlassian.com/manage-profile/security/api-tokens) sign in.
## ned runs the token command and hands curl the credentials through a -K
## config file, so the token never reaches this plugin or an argv.
##
## A panel's :query is JQL; an empty one lists your unresolved issues. Jira
## descriptions and comments are Atlassian Document Format, rendered here
## as Markdown.

(def- jira/list-fields "summary,status,assignee,labels,updated")
(def- jira/view-fields (string jira/list-fields ",description,comment"))
# /search/jql refuses a query with no restriction, so an empty one can't mean "everything".
(def- jira/default-jql "assignee = currentUser() AND statusCategory != Done ORDER BY updated DESC")
(def- jira/key-peg (peg/compile ~(* (range "AZ") (any (+ (range "AZ" "09") "_")) "-" :d+ -1)))

(defn- jira/site [connection]
  (def url (string/trim (or (connection :url) "") " /"))
  (unless (string/has-prefix? "https://" url)
    (error (string "connection \"" (connection :name) "\" needs an https:// :url, https://SITE.atlassian.net")))
  url)

(defn- jira/command [url & args]
  {:argv ["curl" "-sS" "--fail-with-body" "--proto" "=https" "--max-time" "60"
          "-H" "Accept: application/json" ;args url]
   :curl-credentials true})

## Atlassian Document Format -> Markdown. Unknown nodes degrade to their text.

(defn- adf/date [timestamp]
  (if-let [ms (if (string? timestamp) (scan-number timestamp) timestamp)]
    (let [d (os/date (math/floor (/ ms 1000)))]
      (string/format "%d-%02d-%02d" (d :year) (inc (d :month)) (inc (d :month-day))))
    ""))

(defn- adf/marked [text marks]
  (def by-type (tabseq [m :in (or marks [])] (m :type) m))
  (var out text)
  (when (by-type "code") (set out (string "`" out "`")))
  (when (by-type "strike") (set out (string "~~" out "~~")))
  (when (by-type "em") (set out (string "*" out "*")))
  (when (by-type "strong") (set out (string "**" out "**")))
  (when-let [link (by-type "link")
             href (get-in link [:attrs :href])]
    (set out (string "[" out "](" href ")")))
  out)

(defn- adf/inline [node]
  (def attrs (or (node :attrs) {}))
  (case (node :type)
    "text" (adf/marked (or (node :text) "") (node :marks))
    "hardBreak" "\n"
    "mention" (or (attrs :text) (string "@" (attrs :id)))
    "emoji" (or (attrs :text) (attrs :shortName) "")
    "inlineCard" (or (attrs :url) "")
    "status" (string "[" (or (attrs :text) "") "]")
    "date" (adf/date (attrs :timestamp))
    "mediaInline" "[attachment]"
    "placeholder" ""
    (string/join (map adf/inline (or (node :content) [])) "")))

(defn- adf/inlines [node]
  (string/join (map adf/inline (or (node :content) [])) ""))

(defn- adf/prefix-lines
  "text with first before its first line and rest before each later one."
  [text first rest]
  (def out @[])
  (eachp [i line] (string/split "\n" text)
    (def prefix (if (= i 0) first rest))
    (array/push out (if (empty? line) (string/trimr prefix) (string prefix line))))
  (string/join out "\n"))

(defn- adf/block [node]
  (def attrs (or (node :attrs) {}))
  (def content (or (node :content) []))
  (defn children [separator]
    (string/join (filter |(not (empty? $)) (map adf/block content)) separator))
  (defn items [marker-of body-of]
    (def out @[])
    (eachp [i item] content
      (def marker (marker-of i item))
      (array/push out (adf/prefix-lines (body-of item) marker (string/repeat " " (length marker)))))
    (string/join out "\n"))
  (defn list-item-body [item]
    (string/join (filter |(not (empty? $)) (map adf/block (or (item :content) []))) "\n"))
  (defn inline-item-body [item]
    # A task or decision item holds inline nodes, and may hold a nested list.
    (string/join (map |(if (index-of ($ :type) ["taskList" "decisionList"]) (string "\n" (adf/block $)) (adf/inline $))
                      (or (item :content) []))
                 ""))
  (case (node :type)
    "doc" (children "\n\n")
    "paragraph" (adf/inlines node)
    "heading" (string (string/repeat "#" (or (attrs :level) 1)) " " (adf/inlines node))
    "bulletList" (items (fn [_ _] "- ") list-item-body)
    "orderedList" (let [start (or (attrs :order) 1)]
                    (items (fn [i _] (string (+ start i) ". ")) list-item-body))
    "taskList" (items (fn [_ item] (if (= (get-in item [:attrs :state]) "DONE") "- [x] " "- [ ] ")) inline-item-body)
    "decisionList" (items (fn [_ _] "- ") inline-item-body)
    "listItem" (list-item-body node)
    "codeBlock" (string "```" (or (attrs :language) "") "\n"
                        (string/join (map |(or ($ :text) "") content) "") "\n```")
    "blockquote" (adf/prefix-lines (children "\n\n") "> " "> ")
    "panel" (let [kind (or (attrs :panelType) "note")]
              (adf/prefix-lines (string "**" (string/ascii-upper (string/slice kind 0 1)) (string/slice kind 1) "**\n\n"
                                        (children "\n\n"))
                                "> " "> "))
    "rule" "---"
    "expand" (string "**" (or (attrs :title) "Details") "**\n\n" (children "\n\n"))
    "nestedExpand" (string "**" (or (attrs :title) "Details") "**\n\n" (children "\n\n"))
    "mediaSingle" (children "\n")
    "mediaGroup" (children "\n")
    "media" (if-let [alt (attrs :alt)] (string "[attachment: " alt "]") "[attachment]")
    "caption" (adf/inlines node)
    "blockCard" (or (attrs :url) "")
    "embedCard" (or (attrs :url) "")
    "table" (let [rows (map (fn [row]
                              (map (fn [cell]
                                     (def text (string/join (filter |(not (empty? $)) (map adf/block (or (cell :content) [])))
                                                            " "))
                                     (string/replace-all "|" "\\|" (string/replace-all "\n" " " text)))
                                   (or (row :content) [])))
                            content)
                  width (max 1 ;(map length rows))
                  line (fn [cells]
                         (def padded (array/concat @[] cells (array/new-filled (- width (length cells)) "")))
                         (string "| " (string/join padded " | ") " |"))
                  out @[]]
              (eachp [i cells] rows
                (array/push out (line cells))
                (when (= i 0) (array/push out (line (array/new-filled width "---")))))
              (string/join out "\n"))
    (if (empty? content) (adf/inline node) (children "\n\n"))))

(defn- adf/markdown [doc]
  (cond
    (dictionary? doc) (adf/block doc)
    (string? doc) doc
    ""))

## Markdown -> Atlassian Document Format, for what's written in ned: a
## comment. Covers what a comment is typed with -- paragraphs (a single
## newline is a line break, as in a GitHub comment), headings, fenced code,
## quotes, rules, nested bullet and numbered lists, and **strong**, *em*,
## ~~strike~~, `code`, [links](url) and bare URLs. Anything else is text.

(defn- md/url-end
  "Where a bare URL starting at i ends: at whitespace, less trailing punctuation."
  [text i]
  (var j i)
  (while (and (< j (length text)) (not (index-of (text j) [32 9 10]))) (++ j))
  (while (and (> j i) (index-of (text (dec j)) (string/bytes ".,;:!?)]'\""))) (-- j))
  j)

(defn- md/word-char? [c]
  (and c (or (<= 48 c 57) (<= 65 c 90) (<= 97 c 122))))

(defn- md/inline [text marks]
  (def out @[])
  (def plain @"")
  (defn flush []
    (unless (empty? plain)
      (array/push out (if (empty? marks) {:type "text" :text (string plain)} {:type "text" :text (string plain) :marks marks}))
      (buffer/clear plain)))
  (defn wrapped [inner mark]
    (flush)
    (array/concat out (md/inline inner [;marks mark])))
  (def n (length text))
  (var i 0)
  (while (< i n)
    (def c (text i))
    (def rest (string/slice text i))
    (cond
      (and (= c (chr "\\")) (< (inc i) n))
      (do (buffer/push-byte plain (text (inc i))) (+= i 2))

      (= c (chr "`"))
      (if-let [close (string/find "`" text (inc i))]
        (do
          (flush)
          (def code (string/slice text (inc i) close))
          (unless (empty? code)
            (array/push out {:type "text" :text code :marks [{:type "code"}]}))
          (set i (inc close)))
        (do (buffer/push-byte plain c) (++ i)))

      (and (or (string/has-prefix? "**" rest) (string/has-prefix? "__" rest) (string/has-prefix? "~~" rest))
           (string/find (string/slice rest 0 2) text (+ i 2)))
      (let [delimiter (string/slice rest 0 2)
            close (string/find delimiter text (+ i 2))]
        (if (= close (+ i 2))
          (do (buffer/push-string plain delimiter) (+= i 2))
          (do
            (wrapped (string/slice text (+ i 2) close) {:type (if (= delimiter "~~") "strike" "strong")})
            (set i (+ close 2)))))

      (and (or (= c (chr "*")) (and (= c (chr "_")) (not (md/word-char? (get text (dec i))))))
           (md/word-char? (get text (inc i))))
      (let [delimiter (string/from-bytes c)
            close (string/find delimiter text (inc i))]
        (if (and close (or (= c (chr "*")) (not (md/word-char? (get text (inc close))))))
          (do
            (wrapped (string/slice text (inc i) close) {:type "em"})
            (set i (inc close)))
          (do (buffer/push-byte plain c) (++ i))))

      (and (= c (chr "[")) (string/find "](" text i))
      (let [middle (string/find "](" text i)
            close (string/find ")" text middle)]
        (if (and close (not (string/find "\n" (string/slice text i close))))
          (do
            (wrapped (string/slice text (inc i) middle) {:type "link" :attrs {:href (string/slice text (+ middle 2) close)}})
            (set i (inc close)))
          (do (buffer/push-byte plain c) (++ i))))

      (and (or (string/has-prefix? "https://" rest) (string/has-prefix? "http://" rest))
           (not (md/word-char? (get text (dec i)))))
      (let [end (md/url-end text i)
            url (string/slice text i end)]
        (flush)
        (array/push out {:type "text" :text url :marks [;marks {:type "link" :attrs {:href url}}]})
        (set i end))

      (do (buffer/push-byte plain c) (++ i))))
  (flush)
  out)

(defn- md/inline-lines
  "A paragraph's lines, each line break kept."
  [lines]
  (def out @[])
  (eachp [k line] lines
    (when (> k 0) (array/push out {:type "hardBreak"}))
    (array/concat out (md/inline (string/trimr line) [])))
  out)

(def- md/fence-peg (peg/compile ~(* (any " ") (<- (+ "```" "~~~")) (any " ") (<- (any 1)))))
(def- md/heading-peg (peg/compile ~(* (<- (between 1 6 "#")) (some " ") (<- (any 1)))))
(def- md/rule-peg (peg/compile ~(* (between 0 3 " ") (+ (at-least 3 (* "-" (any " "))) (at-least 3 (* "*" (any " "))) (at-least 3 (* "_" (any " ")))) -1)))
# indent, marker, the spaces after it, the item's first line
(def- md/bullet-peg (peg/compile ~(* (<- (any " ")) (<- (set "-*+")) (<- (some " ")) (<- (any 1)))))
(def- md/ordered-peg (peg/compile ~(* (<- (any " ")) (<- :d+) (set ".)") (<- (some " ")) (<- (any 1)))))

(defn- md/blank? [line] (empty? (string/trim line)))

(defn- md/list-marker
  "[kind width first-line start] for a list item line, else nil."
  [line]
  (if-let [[indent marker spaces text] (peg/match md/bullet-peg line)]
    (unless (peg/match md/rule-peg line)
      [:bullet (+ (length indent) 1 (length spaces)) text nil])
    (when-let [[indent number spaces text] (peg/match md/ordered-peg line)]
      [:ordered (+ (length indent) (length number) 1 (length spaces)) text (scan-number number)])))

(defn- md/starts-block? [line]
  (or (peg/match md/fence-peg line) (peg/match md/heading-peg line) (peg/match md/rule-peg line)
      (string/has-prefix? ">" (string/triml line)) (md/list-marker line)))

(defn- md/dedent [line width]
  (var k 0)
  (while (and (< k width) (< k (length line)) (= (line k) 32)) (++ k))
  (string/slice line k))

(var- md/blocks nil)

(defn- md/list
  "The list starting at lines[i]: [node next-index]."
  [lines i]
  (def [kind] (md/list-marker (lines i)))
  (def items @[])
  (var start nil)
  (var k i)
  (var done false)
  (while (and (not done) (< k (length lines)))
    (def marker (md/list-marker (lines k)))
    (if (or (nil? marker) (not= (marker 0) kind))
      (set done true)
      (let [[_ width first number] marker
            body @[first]]
        (when (nil? start) (set start number))
        (++ k)
        (var item-done false)
        (while (and (not item-done) (< k (length lines)))
          (def line (lines k))
          (cond
            (md/blank? line)
            # A blank line continues the item only when what follows is indented into it.
            (if-let [next (find-index |(not (md/blank? $)) (array/slice lines k))]
              (if (>= (- (length (lines (+ k next))) (length (string/triml (lines (+ k next))))) width)
                (do (array/push body "") (++ k))
                (do (set item-done true) (set done (not (md/list-marker (lines (+ k next))))) (+= k next)))
              (do (set item-done true) (set done true)))
            (>= (- (length line) (length (string/triml line))) width)
            (do (array/push body (md/dedent line width)) (++ k))
            (md/starts-block? line)
            (set item-done true)
            (do (array/push body line) (++ k))))
        (def content (md/blocks body))
        (array/push items {:type "listItem" :content (if (empty? content) [{:type "paragraph" :content []}] content)}))))
  (def node (if (= kind :bullet)
              {:type "bulletList" :content items}
              {:type "orderedList" :attrs {:order (or start 1)} :content items}))
  [node k])

(set md/blocks
  (fn [lines]
    (def out @[])
    (var i 0)
    (while (< i (length lines))
      (def line (lines i))
      (cond
        (md/blank? line) (++ i)

        (peg/match md/fence-peg line)
        (let [[fence language] (peg/match md/fence-peg line)
              code @[]]
          (++ i)
          (while (and (< i (length lines)) (not (string/has-prefix? fence (string/triml (lines i)))))
            (array/push code (lines i))
            (++ i))
          (++ i)
          (def text (string/join code "\n"))
          (array/push out (merge {:type "codeBlock" :content (if (empty? text) [] [{:type "text" :text text}])}
                                 (if (empty? (string/trim language)) {} {:attrs {:language (string/trim language)}}))))

        (peg/match md/heading-peg line)
        (let [[hashes text] (peg/match md/heading-peg line)]
          (array/push out {:type "heading" :attrs {:level (length hashes)} :content (md/inline (string/trim text) [])})
          (++ i))

        (peg/match md/rule-peg line)
        (do (array/push out {:type "rule"}) (++ i))

        (string/has-prefix? ">" (string/triml line))
        (let [quoted @[]]
          (while (and (< i (length lines)) (string/has-prefix? ">" (string/triml (lines i))))
            (def inner (string/slice (string/triml (lines i)) 1))
            (array/push quoted (if (string/has-prefix? " " inner) (string/slice inner 1) inner))
            (++ i))
          (array/push out {:type "blockquote" :content (md/blocks quoted)}))

        (md/list-marker line)
        (let [[node next] (md/list lines i)]
          (array/push out node)
          (set i next))

        (let [paragraph @[line]]
          (++ i)
          (while (and (< i (length lines)) (not (md/blank? (lines i))) (not (md/starts-block? (lines i))))
            (array/push paragraph (lines i))
            (++ i))
          (array/push out {:type "paragraph" :content (md/inline-lines paragraph)}))))
    out))

(defn- md/adf [markdown]
  {:version 1 :type "doc" :content (md/blocks (string/split "\n" (string/replace-all "\r\n" "\n" markdown)))})

(defn- jira/browse-url [i]
  (when-let [self (i :self)
             at (string/find "/rest/" self)]
    (string (string/slice self 0 at) "/browse/" (i :key))))

(defn- jira/issue [i]
  (def f (or (i :fields) {}))
  {:key (i :key)
   :title (f :summary)
   :status (get-in f [:status :name])
   :assignee (get-in f [:assignee :displayName])
   :labels (or (f :labels) [])
   :url (jira/browse-url i)
   :updated (f :updated)})

(defn- jira/list-argv [connection query]
  (def jql (if (empty? (string/trim query)) jira/default-jql query))
  (jira/command (string (jira/site connection) "/rest/api/3/search/jql")
                "-G"
                "--data-urlencode" (string "jql=" jql)
                "--data-urlencode" (string "fields=" jira/list-fields)
                "--data-urlencode" "maxResults=100"))

(defn- jira/parse-list [output]
  (map jira/issue (or ((ned/json-decode output) :issues) [])))

(defn- jira/checked-key
  "key upper-cased, if it is one: it goes into URL paths and queries."
  [key]
  (def issue-key (string/ascii-upper (string/trim key)))
  (unless (peg/match jira/key-peg issue-key)
    (error (string "\"" key "\" isn't a Jira issue key")))
  issue-key)

(defn- jira/issue-url [connection key]
  (string (jira/site connection) "/rest/api/3/issue/" (jira/checked-key key)))

(defn- jira/view-argv [connection key]
  (jira/command (jira/issue-url connection key)
                "-G" "--data-urlencode" (string "fields=" jira/view-fields)))

(defn- jira/parse-view [output]
  (def i (ned/json-decode output))
  (def f (or (i :fields) {}))
  (merge (jira/issue i)
         {:body (adf/markdown (f :description))
          :comments (map (fn [c] {:author (get-in c [:author :displayName])
                                  :created (c :created)
                                  :body (adf/markdown (c :body))})
                         (or (get-in f [:comment :comments]) []))}))

(defn- jira/send
  "A credentialed request carrying body as JSON."
  [method url body]
  (merge (jira/command url "-X" method "-H" "Content-Type: application/json" "--data-binary" "@{input-file}")
         {:input (ned/json-encode body)}))

(defn- jira/comment-argv [connection key body]
  (jira/send "POST" (string (jira/issue-url connection key) "/comment") {:body (md/adf body)}))

(defn- jira/transitions-argv [connection key]
  (jira/command (string (jira/issue-url connection key) "/transitions")))

(defn- jira/parse-transitions [output]
  (map (fn [t]
         # A transition is named for the move ("Start work"); its target status says where it goes.
         (def to (get-in t [:to :name]))
         {:id (t :id)
          :name (if (and to (not= to (t :name))) (string (t :name) " → " to) (t :name))})
       (or ((ned/json-decode output) :transitions) [])))

(defn- jira/transition-argv [connection key id]
  (jira/send "POST" (string (jira/issue-url connection key) "/transitions") {:transition {:id id}}))

(defn- jira/assignees-argv [connection key]
  (jira/command (string (jira/site connection) "/rest/api/3/user/assignable/search")
                "-G"
                "--data-urlencode" (string "issueKey=" (jira/checked-key key))
                "--data-urlencode" "maxResults=100"))

(defn- jira/parse-assignees [output]
  (array/push
    (map (fn [u] {:id (u :accountId) :name (u :displayName) :email (u :emailAddress)})
         (filter |(not= ($ :accountType) "app") (ned/json-decode output)))
    {:id "" :name "Unassigned"}))

(defn- jira/assign-argv [connection key id]
  (def url (string (jira/issue-url connection key) "/assignee"))
  (if (empty? id)
    # Unassigning sends a JSON null, which a Janet table can't hold.
    (merge (jira/send "PUT" url {}) {:input `{"accountId":null}`})
    (jira/send "PUT" url {:accountId id})))

(defn- jira/worklog-argv [connection key started seconds]
  (def d (os/date started))
  (jira/send "POST" (string (jira/issue-url connection key) "/worklog")
             {:timeSpentSeconds seconds
              :started (string/format "%04d-%02d-%02dT%02d:%02d:%02d.000+0000"
                                      (d :year) (inc (d :month)) (inc (d :month-day)) (d :hours) (d :minutes) (d :seconds))}))

(defn- jira/project-keys-argv [connection]
  (jira/command (string (jira/site connection) "/rest/api/3/project/search")
                "-G" "--data-urlencode" "maxResults=100"))

(defn- jira/parse-project-keys [output]
  (filter string? (map |($ :key) (or ((ned/json-decode output) :values) []))))

(ned/tracker-register-provider "jira"
  {:list-argv jira/list-argv
   :parse-list jira/parse-list
   :view-argv jira/view-argv
   :parse-view jira/parse-view
   :comment-argv jira/comment-argv
   :transitions-argv jira/transitions-argv
   :parse-transitions jira/parse-transitions
   :transition-argv jira/transition-argv
   :assignees-argv jira/assignees-argv
   :parse-assignees jira/parse-assignees
   :assign-argv jira/assign-argv
   :worklog-argv jira/worklog-argv
   :project-keys-argv jira/project-keys-argv
   :parse-project-keys jira/parse-project-keys
   # Closed ones too: a commit may well name the issue it just finished.
   :mine-query "assignee = currentUser() OR watcher = currentUser() ORDER BY updated DESC"})
