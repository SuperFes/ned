# Language Support

Every bundled language gets tree-sitter-driven syntax highlighting, folding, indentation,
and (for most) a symbol gutter, with no configuration — see
[Language Intelligence](features/language-intelligence.md) for what that gets you for
free. LSP/DAP/test-runner integration is a separate, explicit step: ned never bundles or
auto-detects a language server, debug adapter, or test command, so the recipes below are
what to paste into `init.janet` for each language, once you have the relevant tool
already installed.

For the full catalogue of bundled languages, admission policy, and depth tiers (how deep
"supported" goes for a given language, and why), see the
[Language Coverage](../../dev/LanguageCoverage.html) chapter of the developer docs —
that's more detail than most people configuring a language server need, but it's the
place to check if you're wondering whether a language you care about is covered at all
or considering contributing one.

{{#include ../../Docs/LanguageSetup.md}}
