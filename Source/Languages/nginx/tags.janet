#; Symbol-kind query. The grammar ships highlights and injections only. A
#; config's structure is its blocks: http/server/upstream/map scope everything
#; under them, and a location is what a reader actually scrolls looking for.

((attribute
   (keyword) @name) @definition.module
 (:any-of? @name "http" "events" "server" "upstream" "stream" "mail" "map" "types" "geo" "split_clients" "limit_req_zone"))

(location
  (location_route) @name) @definition.module
