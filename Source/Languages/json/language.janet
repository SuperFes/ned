# No :line-comment -- JSON has no comment syntax at all, real or otherwise;
# toggle-line-comment correctly reports nothing configured rather than
# inserting something that would make the file invalid JSON.

{:name "json"
 :extensions [".json"]
}
