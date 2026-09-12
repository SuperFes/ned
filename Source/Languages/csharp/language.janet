{:name "csharp"
 :extensions [".cs"]
 :line-comment "//"

 # .NET has no fixed-name project marker: a "*.<ext>" entry means "any
 # file with this extension" (RootResolver's MarkerExistsInDirectory).
 # global.json is first since it's a cheap exists() rather than a scan.
 :lsp-root-markers ["global.json" "*.csproj" "*.sln"]
}
