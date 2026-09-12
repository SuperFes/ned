# No :line-comment -- HTML only has block comments (<!-- -->).

{:name "html"
 :extensions [".html" ".htm"]

 # <script>/<style> regions sync to their own languages' real LSP servers
 # (Editor/EmbeddedDocuments.h).
 :embedded-documents true
 :snippets
 {
   "html5"
   "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <title>${1:Document}</title>\n</head>\n<body>\n    $0\n</body>\n</html>"
   "div"
   "<div class=\"${1:class}\">\n    $0\n</div>"
   "a"
   "<a href=\"${1:#}\">${2:text}</a>$0"
   "img"
   "<img src=\"${1:src}\" alt=\"${2:alt}\">$0"
   "link"
   "<link rel=\"stylesheet\" href=\"${1:style.css}\">$0"
   "script"
   "<script src=\"${1:script.js}\"></script>$0"}
}
