# The .http / .rest request-file format (rest.nvim, VS Code REST Client,
# JetBrains HTTP client).
{:name "http"
 :extensions [".http" ".rest"]
 :line-comment "#"
 :not-applicable {:indents    "requests are flat; bodies indent as their own language"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "a request is sent as written"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
