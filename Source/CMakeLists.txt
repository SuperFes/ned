# ned_lib (everything except main()) and the ned executable. Added via
# add_subdirectory(Source) from the root CMakeLists.txt, after
# CMake/Dependencies.cmake, CMake/Grammars.cmake and CMake/EmbedGenerated.cmake
# have already run -- this file links against targets/variables all three
# produce (notcurses-core, vterm, utf8proc, nlohmann_json::nlohmann_json,
# re2::re2, pcre2-8-static, the tree-sitter-* per-language grammar targets,
# Janet_*, NED_GENERATED_QUERY_SOURCES).

add_library(ned_lib STATIC
        ${NED_GENERATED_QUERY_SOURCES}
        Application.cpp
        Text/Rope.cpp
        Text/RopeStorage.cpp
        Text/MappedFile.cpp
        Text/OffsetRemap.cpp
        Text/PieceTable.cpp
        Text/PieceTableStorage.cpp
        Text/DiskSpace.cpp
        Text/FilePreservation.cpp
        Text/Grapheme.cpp
        Text/UndoTree.cpp
        Text/KillRing.cpp
        Text/Buffer.cpp
        Text/BufferList.cpp
        Text/BinaryDetect.cpp
        Text/LineDiff.cpp
        Text/ThreeWayMerge.cpp
        Text/ConflictHunk.cpp
        Text/Utf8.cpp
        Text/LineEnding.cpp
        Text/WhitespaceHygiene.cpp
        Editor/Key.cpp
        Editor/Command.cpp
        Editor/FuzzyMatch.cpp
        Editor/SearchEverywhere.cpp
        Editor/Keymap.cpp
        Editor/Dispatcher.cpp
        Editor/Commands.cpp
        Editor/ScriptingSession.cpp
        Editor/IncrementalSearch.cpp
        Editor/LineListSearch.cpp
        Editor/PrefixArgument.cpp
        Editor/QueryReplace.cpp
        Editor/RegexPattern.cpp
        Editor/HugeRegexScan.cpp
        Editor/Register.cpp
        Editor/MacroRegistry.cpp
        Editor/Rectangle.cpp
        Editor/ConflictResolution.cpp
        Editor/AutoPair.cpp
        Editor/CompletionSession.cpp
        Editor/DabbrevComplete.cpp
        Editor/JanetSymbolComplete.cpp
        Editor/MinibufferPrompt.cpp
        Editor/PromptHistory.cpp
        Editor/Mode.cpp
        Editor/LanguageDefinition.cpp
        Editor/LanguageFiles.cpp
        Editor/JanetData.cpp
        Editor/LanguageParse.cpp
        Editor/LanguageRegistry.cpp
        Editor/CaptureClassifiers.cpp
        Editor/QueryData.cpp
        Editor/BundledLanguages.cpp
        Editor/Languages/CLike.cpp
        Editor/Languages/Markdown.cpp
        Editor/Languages/Org.cpp
        Editor/Injection.cpp
        Editor/EmbeddedDocuments.cpp
        Editor/SyntaxTheme.cpp
        Editor/ModeOverrides.cpp
        Editor/ModePrewarm.cpp
        Editor/Multibuffer.cpp
        Editor/MultibufferFoldSettings.cpp
        Editor/MultibufferLimits.cpp
        Editor/MultibufferSearchSettings.cpp
        Editor/NextError.cpp
        Editor/WrapOverrides.cpp
        Editor/WrapIndent.cpp
        Editor/CodeFold.cpp
        Editor/LocalScopes.cpp
        Editor/RenameReview.cpp
        Editor/ClassFileSync.cpp
        Editor/ImportFixup.cpp
        Editor/ImportFixupSettings.cpp
        Editor/RenameReviewSettings.cpp
        Editor/SearchEverywhereGestureSettings.cpp
        Editor/SearchEverywhereTextSearchSettings.cpp
        Editor/ClassFileSyncSettings.cpp
        Editor/CodeFoldSettings.cpp
        Editor/StickyScroll.cpp
        Editor/StickyScrollSettings.cpp
        Editor/RelativeLineNumberSettings.cpp
        Editor/WhichKeySettings.cpp
        Editor/Org.cpp
        Editor/OrgCapture.cpp
        Editor/Snippet.cpp
        Editor/SnippetRegistry.cpp
        Editor/BundledSnippets.cpp
        Editor/LinkedEditingSession.cpp
        Editor/Table.cpp
        Editor/Markdown.cpp
        Editor/Link.cpp
        Editor/Php.cpp
        Editor/HeaderSource.cpp
        Editor/FinalNewline.cpp
        Editor/TrimOnSave.cpp
        Editor/LineEndingPolicy.cpp
        Editor/FormatOnSave.cpp
        Editor/FormatConfigParse.cpp
        Editor/FormatRules.cpp
        Editor/FormatEdit.cpp
        Editor/FormatBracePlacement.cpp
        Editor/FormatSpacing.cpp
        Editor/FormatBlankLines.cpp
        Editor/FormatWrap.cpp
        Editor/FormatCase.cpp
        Editor/Format.cpp
        Editor/AutoFormatOnSave.cpp
        Editor/ScopedFormat.cpp
        Editor/Clipboard.cpp
        Editor/TabWidth.cpp
        Editor/IndentStyle.cpp
        Editor/IndentDefaults.cpp
        Editor/IndentDetect.cpp
        Editor/Indent.cpp
        Editor/IndentRuleOverride.cpp
        Editor/HugeFileReindent.cpp
        Editor/BlankLineCleanup.cpp
        Editor/MaxConsecutiveBlankLines.cpp
        Editor/FillColumn.cpp
        Editor/Fill.cpp
        Editor/PageScroll.cpp
        Editor/DiffRefreshSettings.cpp
        Editor/ProcessTimeouts.cpp
        Editor/ThemeSetting.cpp
        Editor/Variables.cpp
        Editor/HighlightSettings.cpp
        Editor/HugeStructuralWindow.cpp
        Editor/MinimapSettings.cpp
        Editor/RulerSettings.cpp
        Editor/WhitespaceSettings.cpp
        Editor/GitIgnore.cpp
        Editor/SearchSettings.cpp
        Editor/Project/Search.cpp
        Editor/Project/Agenda.cpp
        Editor/Project/CaseCheck.cpp
        Editor/Project/CaseViolationsBuffer.cpp
        Editor/Project/Replace.cpp
        Editor/Project/Tree.cpp
        Editor/Project/Root.cpp
        Editor/Project/Settings.cpp
        Editor/ToolchainIncludePaths.cpp
        Editor/NodeModules.cpp
        Editor/ImportResolutionConfig.cpp
        Editor/ImportResolve.cpp
        Editor/Project/FileOps.cpp
        Editor/AutoRevert.cpp
        Editor/AutoMerge.cpp
        Editor/FileWatch.cpp
        Editor/BackgroundActivity.cpp
        Editor/HighlightCache.cpp
        Editor/ChunkedHighlight.cpp
        Editor/InlineDiagnostics.cpp
        Editor/RecencyGlow.cpp
        Editor/ScratchPad.cpp
        Editor/Backup.cpp
        Editor/CliFormatDispatch.cpp
        Editor/BufferSave.cpp
        Editor/PersistentUndo.cpp
        Editor/Project/Undo.cpp
        Editor/DiagnosticsLog.cpp
        Editor/SanitizerOutputParser.cpp
        Editor/ValgrindOutputParser.cpp
        Editor/MassifOutputParser.cpp
        Editor/MassifReportBuffer.cpp
        Editor/Session.cpp
        Editor/RecentFiles.cpp
        Editor/Bookmark.cpp
        Editor/Project/Session.cpp
        Editor/Project/Trust.cpp
        Editor/Project/Registry.cpp
        Editor/PendingReExec.cpp
        Editor/TerminalTabLauncher.cpp
        Editor/Project/Switch.cpp
        Editor/Project/Plugins.cpp
        Editor/Vim/Settings.cpp
        Editor/Vim/Motion.cpp
        Editor/Vim/TextObject.cpp
        Editor/Vim/Surround.cpp
        Editor/Vim/Registers.cpp
        Editor/Vim/ExCommand.cpp
        Editor/Vim/Engine.cpp
        Editor/Vim/GlobalMarks.cpp
        Editor/Vim/Magic.cpp
        Editor/Vcs/DiffPatch.cpp
        Editor/Vcs/ProviderRegistry.cpp
        Editor/Vcs/RowStatus.cpp
        Editor/Vcs/Runner.cpp
        Editor/Process/ChildProcess.cpp
        Editor/Acp/Transport.cpp
        Editor/Acp/Client.cpp
        Editor/Acp/Config.cpp
        Editor/Acp/PanelConfig.cpp
        Editor/Acp/Manager.cpp
        Editor/Lsp/Transport.cpp
        Editor/Lsp/Client.cpp
        Editor/Lsp/ServerConfig.cpp
        Editor/Lsp/RootResolver.cpp
        Editor/Lsp/ProseChecker.cpp
        Editor/Lsp/Position.cpp
        Editor/Lsp/Content.cpp
        Editor/Lsp/EditApply.cpp
        Editor/Lsp/Manager.cpp
        Editor/Lsp/BackgroundSync.cpp
        Editor/Lsp/Broker.cpp
        Editor/Lsp/BrokerSocketPath.cpp
        Editor/Lsp/BrokerMain.cpp
        Editor/Lsp/BrokerDaemon.cpp
        Editor/Lsp/BrokerConnect.cpp
        Editor/Mcp/SocketPath.cpp
        Editor/Mcp/Transport.cpp
        Editor/Mcp/ToolRegistry.cpp
        Editor/Mcp/BridgeServer.cpp
        Editor/Mcp/BridgeSetting.cpp
        Editor/Tasks/TaskConfig.cpp
        Editor/Tasks/TaskProcess.cpp
        Editor/Tasks/TaskRunner.cpp
        Editor/Repl/Config.cpp
        Editor/TestRun/TestOutputParser.cpp
        Editor/TestRun/TestResult.cpp
        Editor/TestRun/TestResultsBuffer.cpp
        Editor/TestRun/Config.cpp
        Editor/TestRun/TestRunner.cpp
        Editor/TestRun/TestSourceResolver.cpp
        Editor/Coverage/Report.cpp
        Editor/Coverage/OutputParser.cpp
        Editor/Coverage/Config.cpp
        Editor/Dap/Config.cpp
        Editor/Dap/Client.cpp
        Editor/Dap/Manager.cpp
        Editor/PointerGraphNode.cpp
        Editor/Sparkline.cpp
        Editor/MemoryImage.cpp
        Editor/Terminal/Config.cpp
        Editor/Terminal/Emulator.cpp
        Editor/Terminal/PtyProcess.cpp
        Editor/Grammar/Node.cpp
        Editor/Imprint.cpp
        Editor/ImprintTables.cpp
        Editor/ImprintFold.cpp
        Editor/ImprintBracket.cpp
        Editor/ImprintIndent.cpp
        Editor/Grammar/GrammarImprint.cpp
        Editor/Grammar/Tree.cpp
        Editor/Grammar/Parser.cpp
        Editor/Grammar/IncrementalParse.cpp
        Editor/Grammar/QueryPredicates.cpp
        Editor/Grammar/QueryMatcher.cpp
        Editor/Grammar/MatchCache.cpp
        Editor/Grammar/Languages.cpp
        Editor/Grammar/DynamicGrammar.cpp
        Editor/Parse/Green.cpp
        Editor/Parse/Lexer.cpp
        Editor/Parse/Stack.cpp
        Editor/Parse/Parser.cpp
        Editor/Parse/Tree.cpp
        Editor/Parse/Node.cpp
        Editor/Parse/Cursor.cpp
        Editor/Parse/Sexp.cpp
        Janet/Value.cpp
        Janet/Environment.cpp
        Janet/EditorBindings.cpp
        Janet/JanetVcsProvider.cpp
        Janet/PluginLoader.cpp
        Janet/InitFile.cpp
        UI/KeyTranslation.cpp
        UI/DoubleTapModifier.cpp
        UI/Border.cpp
        UI/Widget.cpp
        UI/Compositing.cpp
        UI/Paint.cpp
        UI/PaintParse.cpp
        UI/ThemePaints.cpp
        UI/EventLoop.cpp
        UI/Layout.cpp
        UI/ScrollBar.cpp
        UI/Minimap.cpp
        UI/Overlay.cpp
        UI/PanelDock.cpp
        UI/TerminalPanel.cpp
        UI/AcpPanel.cpp
        UI/DebugConsolePanel.cpp
        UI/JanetReplPanel.cpp
        UI/ListPopup.cpp
        UI/TreeView.cpp
        UI/MemoryImageView.cpp
        UI/BufferListPanel.cpp
        UI/DapThreadsPanel.cpp
        UI/BufferView.cpp
        UI/BufferView/Paint.cpp
        UI/BufferView/Gutter.cpp
        UI/BufferView/Prompts.cpp
        UI/BufferView/Lsp.cpp
        UI/BufferView/Rename.cpp
        UI/BufferView/ClassFileSync.cpp
        UI/BufferView/VcsDap.cpp
        UI/BufferView/Buffers.cpp
        UI/BufferView/CandidateList.cpp
        UI/BufferView/GutterModel.cpp
        UI/BufferView/Viewport.cpp
        UI/BufferView/Mouse.cpp
        UI/WindowManager.cpp
        UI/AsyncFileLoader.cpp
        UI/HugeFileLoader.cpp
        UI/TabBar.cpp
        UI/ScrollArrowButton.cpp
        UI/ProjectSidebar.cpp
        UI/LeftDock.cpp
        UI/VcsPanel.cpp
        UI/VcsDiffPreview.cpp
        UI/ModeLine.cpp
        UI/EchoArea.cpp
        UI/Theme.cpp
        UI/ThemePalette.cpp
        UI/ThemeRegistry.cpp
        UI/ThemeFile.cpp
        UI/ThemeResolve.cpp
        UI/ThemeGallery.cpp
        UI/DesktopThemeProbe.cpp)

target_include_directories(ned_lib
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_BINARY_DIR}/ned_generated
        PUBLIC ${Janet_INCLUDE_DIRS})

# Janet_LIBRARIES is just the bare library name ("janet"), not a full path --
# pkg_check_modules doesn't add -L for it, so linking relies on the bare
# "-ljanet" resolving via the linker's own implicit default search path.
# That's true often enough to go unnoticed (works with GNU ld's Debian/Ubuntu
# default search list, or when Janet happens to already be installed
# somewhere already on the default path), but broke under lld on a fresh CI
# runner where /usr/local/lib (where CI's own `make install` puts it) isn't
# implicitly searched -- ld.lld: unable to find library -ljanet. Explicit
# target_link_directories is what actually fixes it, not luck.
target_link_directories(ned_lib PUBLIC ${Janet_LIBRARY_DIRS})

target_link_libraries(ned_lib
        PUBLIC ${Janet_LIBRARIES}
        # Every widget (Widget/KeyTranslation/Theme/EventLoop/Layout and every
        # concrete widget through main.cpp's own composition root) is built on
        # Notcurses. Shared, not static (notcurses' static build needs a
        # static libdeflate/zlib this project has no reason to require) --
        # notcurses' own CMakeLists.txt already installs notcurses-core's
        # shared library to CMAKE_INSTALL_LIBDIR unconditionally, so
        # `cmake --install` places it on the normal system library path
        # alongside `ned` with no RPATH trick needed.
        PUBLIC notcurses-core
        # Terminal-panel follow-up: PUBLIC because vterm.h reaches ned_lib's
        # own headers via Source/Editor/Terminal/Emulator.h.
        PUBLIC vterm
        PUBLIC utf8proc
        PUBLIC nlohmann_json::nlohmann_json
        # The tree-sitter RUNTIME is deliberately absent: since the Phase 4b
        # engine swap ned parses with Source/Editor/Parse/ interpreting the
        # generated grammar tables below directly. ned_tests still links the
        # runtime privately as the conformance suite's differential
        # reference.
        PUBLIC tree-sitter-json
        PUBLIC tree-sitter-c
        PUBLIC tree-sitter-cpp
        PUBLIC tree-sitter-php
        PUBLIC tree-sitter-javascript
        PUBLIC tree-sitter-typescript
        PUBLIC tree-sitter-tsx
        PUBLIC tree-sitter-html
        PUBLIC tree-sitter-css
        PUBLIC tree-sitter-python
        PUBLIC tree-sitter-bash
        PUBLIC tree-sitter-janet-simple
        PUBLIC tree-sitter-markdown
        PUBLIC tree-sitter-markdown-inline
        PUBLIC tree-sitter-org
        PUBLIC tree-sitter-yaml
        PUBLIC tree-sitter-toml
        PUBLIC tree-sitter-clojure
        PUBLIC tree-sitter-fish
        PUBLIC tree-sitter-xml
        PUBLIC tree-sitter-rust
        PUBLIC tree-sitter-go
        PUBLIC tree-sitter-c-sharp
        PUBLIC tree-sitter-java
        PUBLIC tree-sitter-kotlin
        PUBLIC tree-sitter-lua
        PUBLIC tree-sitter-cmake
        PUBLIC tree-sitter-diff
        PUBLIC tree-sitter-sql
        PUBLIC tree-sitter-dockerfile
        PUBLIC tree-sitter-make
        PUBLIC tree-sitter-hcl
        PUBLIC tree-sitter-nix
        PUBLIC tree-sitter-ruby
        PUBLIC tree-sitter-gitcommit
        PUBLIC tree-sitter-gitrebase
        PUBLIC tree-sitter-r
        PRIVATE ${CMAKE_DL_LIBS}
        # internal-project-search follow-up: only ProjectSearch.cpp's own
        # implementation includes RE2 headers, so this stays off the public
        # link interface.
        PRIVATE re2::re2
        # in-file-regex follow-up: same treatment for PCRE2 -- only
        # RegexPattern.cpp's implementation includes pcre2.h (the header is
        # pimpl'd specifically so this dependency stays private).
        PRIVATE pcre2-8-static)

if (NED_ENABLE_SANITIZERS)
    target_compile_options(ned_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ned_lib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ned main.cpp)

target_link_libraries(ned PRIVATE ned_lib CLI11::CLI11)

# Packaging follow-up: no install() rule existed at all before this -- fine
# for `cmake --build build && ./build/ned` (this project's own documented
# dev workflow), not fine for anything that needs a real `cmake --install`
# (a system package, an ebuild, ...). Deliberately minimal: just the
# executable, standard GNUInstallDirs layout, no desktop file/icon/man page
# (see ROADMAP.md's "Documentation framework" entry for those).
include(GNUInstallDirs)
# notcurses-core used to be a private FetchContent'd copy, which needed its
# own install(TARGETS ...) plus an INSTALL_RPATH pointing `ned` at a private,
# ned-only subdirectory rather than the shared system libdir -- avoiding a
# real install-time collision with a system notcurses package owning that
# same SONAME path, and pinning the exact version/ABI a system package might
# not carry (see PatchNotcurses*.cmake above). Now that notcurses-core *is*
# the system package (see its own comment above), neither concern applies:
# `ned` links against and finds it exactly the way any other system-library
# consumer does, with the linker's normal default search path.
install(TARGETS ned RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# ned-format argv[0] dispatch follow-up: a relative symlink (main.cpp
# dispatches on argv[0]'s own basename, so it doesn't matter whether it was
# reached via this symlink directly or a bare PATH lookup finding it) --
# relative rather than an absolute CMAKE_INSTALL_PREFIX-baked path, so it
# keeps resolving correctly if the whole prefix is relocated after install
# (a package build re-rooting into a fakeroot, for instance). file(REMOVE)
# first makes a repeated `cmake --install` idempotent -- file(CREATE_LINK)
# does not overwrite an existing path at its destination.
#
# $ENV{DESTDIR} must be prepended by hand here: unlike install(TARGETS)/
# install(FILES)'s generated file(INSTALL ...) calls (which read DESTDIR
# automatically -- see this same file's ned/bin install above), a bare
# file(CREATE_LINK)/file(REMOVE) in install(CODE ...) is an ordinary
# filesystem operation with no install-time DESTDIR awareness at all. Without
# this, a staged install (a distro package build, e.g. Gentoo's portage
# sandbox) tries to write the symlink into the real /usr/bin instead of the
# staging root and fails loudly (sandbox ACCESS DENIED on the live system
# path) rather than landing inside the package image like every other
# installed file.
install(CODE "
    file(REMOVE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/ned-format\")
    file(CREATE_LINK \"ned\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/ned-format\" SYMBOLIC)
")
