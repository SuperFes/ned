# ned_tests (Catch2). Added via add_subdirectory(Tests) from the root
# CMakeLists.txt, inside the NED_BUILD_TESTS guard that already called
# enable_testing()/include(CTest)/include(Catch) and built ned_lib and the
# compiled language tables (CMake/LanguageTables.cmake).

add_executable(ned_tests
            ApplicationTest.cpp
            DataDirTest.cpp
            RopeTest.cpp
            MappedFileTest.cpp
            PieceTableTest.cpp
            PieceTableStorageTest.cpp
            GraphemeTest.cpp
            UndoTreeTest.cpp
            KillRingTest.cpp
            RegisterTest.cpp
            RectangleTest.cpp
            AutoPairTest.cpp
            CompletionSessionTest.cpp
            LspEditApplyTest.cpp
            DabbrevCompleteTest.cpp
            LocalScopesTest.cpp
            ModeLocalsTest.cpp
            RenameReviewTest.cpp
            ClassFileSyncTest.cpp
            ImportFixupTest.cpp
            BufferViewRenameSymbolTest.cpp
            BufferViewCaseFixTest.cpp
            BufferViewClassFileSyncTest.cpp
            BufferViewRenameReviewTest.cpp
            BufferViewImportFixupTest.cpp
            SnippetTest.cpp
            SnippetRegistryTest.cpp
            MacroRegistryTest.cpp
            BundledSnippetsTest.cpp
            LinkedEditingSessionTest.cpp
            JanetSymbolCompleteTest.cpp
            AutoRevertTest.cpp
            AutoMergeTest.cpp
            FileWatchTest.cpp
            LineDiffTest.cpp
            ThreeWayMergeTest.cpp
            ConflictHunkTest.cpp
            ConflictResolutionTest.cpp
            BufferViewConflictQuickKeyTest.cpp
            BufferViewChoicePromptTest.cpp
            BufferViewPromptDriverTest.cpp
            BufferTest.cpp
            BufferHugeFileTest.cpp
            DiskSpaceTest.cpp
            BufferSaveEquivalenceTest.cpp
            SavePlanTest.cpp
            BufferSaveStateTest.cpp
            AsyncSaveDispatchTest.cpp
            ProgressBarTest.cpp
            ApplicationExitTest.cpp
            AsyncFileSaverTest.cpp
            StorageSnapshotTest.cpp
            WhitespaceHygieneTest.cpp
            MultiCursorTest.cpp
            BufferListTest.cpp
            FilePreservationTest.cpp
            BinaryDetectTest.cpp
            LineEndingTest.cpp
            LineEndingPolicyTest.cpp
            KeyTest.cpp
            DoubleTapModifierTest.cpp
            CommandTest.cpp
            FuzzyMatchTest.cpp
            SearchEverywhereTest.cpp
            KeymapTest.cpp
            BindingsReportTest.cpp
            DispatcherTest.cpp
            CommandsTest.cpp
            JanetTestSupport.cpp
            ValueTest.cpp
            EnvironmentTest.cpp
            ScriptingSessionTest.cpp
            EditorBindingsTest.cpp
            JanetVcsProviderTest.cpp
            GitVcsPluginTest.cpp
            InitFileTest.cpp
            TestEvents.cpp
            KeyTranslationTest.cpp
            BufferViewTest.cpp
            BufferViewBlameGutterTest.cpp
            BufferViewDiffGutterTest.cpp
            BufferViewHunkNavigationTest.cpp
            BufferViewIndentGuideDepthTest.cpp
            BufferViewTabGlyphTest.cpp
            BufferViewStickyScrollTest.cpp
            BufferViewSymbolGutterTest.cpp
            BufferViewHugeStructuralGutterTest.cpp
            BufferViewVcsStatusTest.cpp
            BufferViewVcsCommitTest.cpp
            WindowManagerTest.cpp
            ModeLineTest.cpp
            EchoAreaTest.cpp
            Utf8Test.cpp
            IncrementalSearchTest.cpp
            LineListSearchTest.cpp
            PrefixArgumentTest.cpp
            QueryReplaceTest.cpp
            RegexPatternTest.cpp
            HugeRegexScanTest.cpp
            ModeTest.cpp
            LanguageDefinitionTest.cpp
            QueryDataTest.cpp
            QueryMatcherTest.cpp
            MatchCacheTest.cpp
            LanguageParseTest.cpp
            CaptureClassifierTest.cpp
            JanetCaptureClassifierTest.cpp
            CodeFoldTest.cpp
            IndentStyleTest.cpp
            IndentDefaultsTest.cpp
            IndentDetectTest.cpp
            ScopedFormatTest.cpp
            IndentRuleOverrideTest.cpp
            HugeFileReindentTest.cpp
            MaxConsecutiveBlankLinesTest.cpp
            FormatConfigParseTest.cpp
            FormatRulesTest.cpp
            FormatBracePlacementTest.cpp
            FormatSpacingTest.cpp
            FormatBlankLinesTest.cpp
            FormatWrapTest.cpp
            FormatCaseTest.cpp
            FormatAlignTest.cpp
            FormatArrangeTest.cpp
            FormatRewriteTest.cpp
            FileNamingTest.cpp
            FormatTest.cpp
            FormatKeyDocsTest.cpp
            IndentEngineTest.cpp
            IndentTest.cpp
            StickyScrollTest.cpp
            OrgTest.cpp
            OrgHighlightTest.cpp
            OrgCaptureTest.cpp
            OrgClockTest.cpp
            TableTest.cpp
            MarkdownTest.cpp
            InlayHintColumnTest.cpp
            MarkdownHighlightTest.cpp
            InjectionTest.cpp
            EmbeddedDocumentsTest.cpp
            HtmlHighlightTest.cpp
            LinkTest.cpp
            HeaderSourceTest.cpp
            PerformanceTest.cpp
            CompositingTest.cpp
            PaintTest.cpp
            PaintParseTest.cpp
            BundledPaintsTest.cpp
            ChromeSurfaceTest.cpp
            ThemeTest.cpp
            ThemePaletteTest.cpp
            VariablesTest.cpp
            BundledThemesTest.cpp
            SyntaxThemeTest.cpp
            ThemeFileTest.cpp
            ThemeResolveTest.cpp
            ThemeGalleryTest.cpp
            DocSourcePathsTest.cpp
            OracleTest.cpp
            ImprintTest.cpp
            GrammarFileTest.cpp
            ImprintIndentTest.cpp
            CommandReferenceTest.cpp
            ImprintBracketTest.cpp
            FormatterPropertiesTest.cpp
            ThemeKeyDocsTest.cpp
            InitFileThemeWriteTest.cpp
            LayoutStabilityProbe.cpp
            OffsetRemapTest.cpp
            EditJournalTest.cpp
            BufferEditJournalTest.cpp
            AnchorSetTest.cpp
            BufferTrackedStateTest.cpp
            OverlayScrimTest.cpp
            OverlayShadowTest.cpp
            RecencyGlowTest.cpp
            HighlightCacheTest.cpp
            ChunkedHighlightTest.cpp
            KeystrokeBench.cpp
            BufferViewTintCompositingTest.cpp
            DesktopThemeProbeTest.cpp
            ActiveBufferTest.cpp
            CacheStampTest.cpp
            CandidateListTest.cpp
            RequestSlotTest.cpp
            MinibufferPromptTest.cpp
            PromptHistoryTest.cpp
            TabBarTest.cpp
            ScrollArrowButtonTest.cpp
            FinalNewlineTest.cpp
            WrapIndentTest.cpp
            TrimOnSaveTest.cpp
            FormatOnSaveTest.cpp
            ClipboardTest.cpp
            ClipboardTestGuard.cpp
            SigpipeTestGuard.cpp
            TerminalOutputTestGuard.cpp
            TabWidthTest.cpp
            FillColumnTest.cpp
            FillTest.cpp
            PageScrollTest.cpp
            DiffRefreshSettingsTest.cpp
            ProcessTimeoutsTest.cpp
            HighlightSettingsTest.cpp
            HugeStructuralWindowTest.cpp
            MinimapSettingsTest.cpp
            RulerSettingsTest.cpp
            WhitespaceSettingsTest.cpp
            RelativeLineNumberSettingsTest.cpp
            WhichKeySettingsTest.cpp
            ListPopupTest.cpp
            TreeViewTest.cpp
            BufferListPanelTest.cpp
            MinimapTest.cpp
            OverlayTest.cpp
            PanelDockTest.cpp
            GitIgnoreTest.cpp
            SearchSettingsTest.cpp
            ProjectSearchTest.cpp
            ProjectAgendaTest.cpp
            CaseCheckTest.cpp
            ProjectReplaceTest.cpp
            ProjectTreeTest.cpp
            ProjectRootTest.cpp
            ProjectSettingsTest.cpp
            ToolchainIncludePathsTest.cpp
            NodeModulesTest.cpp
            ImportResolutionConfigTest.cpp
            ImportTargetTest.cpp
            PhpTest.cpp
            ProjectFileOpsTest.cpp
            ScratchPadTest.cpp
            BackupTest.cpp
            TransientSessionTest.cpp
            CliFormatDispatchTest.cpp
            LanguageTablesTest.cpp
            ScannersTest.cpp
            LanguagePackageTest.cpp
            LanguageToolsTest.cpp
            PersistentUndoTest.cpp
            ProjectUndoTest.cpp
            DiagnosticsLogTest.cpp
            SanitizerOutputParserTest.cpp
            ValgrindOutputParserTest.cpp
            MassifOutputParserTest.cpp
            MassifReportBufferTest.cpp
            SessionTest.cpp
            RecentFilesTest.cpp
            BookmarkTest.cpp
            ProjectSessionTest.cpp
            ProjectTrustTest.cpp
            ProjectRegistryTest.cpp
            PendingReExecTest.cpp
            TerminalTabLauncherTest.cpp
            ProjectSwitchTest.cpp
            ProjectPluginsTest.cpp
            VcsProviderRegistryTest.cpp
            DiffPatchTest.cpp
            VcsRunnerTest.cpp
            VcsRowStatusTest.cpp
            BackgroundActivityTest.cpp
            AcpTransportTest.cpp
            AcpClientTest.cpp
            AcpConfigTest.cpp
            AcpManagerTest.cpp
            AcpPanelTest.cpp
            FramedConnectionTest.cpp
            LspTransportTest.cpp
            LspClientTest.cpp
            LspServerConfigTest.cpp
            LspRootResolverTest.cpp
            ProseCheckerTest.cpp
            ProseCheckerTestGuard.cpp
            LspPositionTest.cpp
            LspContentTest.cpp
            LspManagerTest.cpp
            ExpandableTreeTest.cpp
            LspBrokerTest.cpp
            LspBrokerConnectTest.cpp
            LspBrokerDaemonTest.cpp
            McpTransportTest.cpp
            McpToolRegistryTest.cpp
            McpBridgeServerTest.cpp
            DapConfigTest.cpp
            DapClientTest.cpp
            DapManagerTest.cpp
            PointerGraphNodeTest.cpp
            SparklineTest.cpp
            MemoryImageTest.cpp
            MemoryImageViewTest.cpp
            DebugConsolePanelTest.cpp
            JanetReplPanelTest.cpp
            DapThreadsPanelTest.cpp
            ChildProcessTest.cpp
            TaskConfigTest.cpp
            TaskProcessTest.cpp
            TaskRunnerTest.cpp
            ReplConfigTest.cpp
            TestOutputParserTest.cpp
            BufferViewTestGutterTest.cpp
            BufferViewCoverageGutterTest.cpp
            TestDiscoveryTest.cpp
            TestResultsBufferTest.cpp
            TestRunConfigTest.cpp
            TestRunnerTest.cpp
            TestSourceResolverTest.cpp
            CoverageOutputParserTest.cpp
            CoverageReportTest.cpp
            CoverageConfigTest.cpp
            JanetTestParserTest.cpp
            TerminalEmulatorTest.cpp
            TerminalPanelTest.cpp
            GrammarTest.cpp
            ParseConformanceTest.cpp
            ModeOverridesTest.cpp
            ModePrewarmTest.cpp
            BufferViewSearchInResultsTest.cpp
            MultibufferTest.cpp
            NextErrorTest.cpp
            BufferViewNextErrorTest.cpp
            BufferViewDiagnosticsBufferTest.cpp
            BufferViewProjectReplaceTest.cpp
            BufferViewResultJumpTest.cpp
            BufferViewEmbeddedLspTest.cpp
            BufferViewCodeLensRowTest.cpp
            BufferViewDocumentLinkTest.cpp
            BufferViewProjectFindReferencesTest.cpp
            BufferViewHierarchyTest.cpp
            WrapOverridesTest.cpp
            BorderTest.cpp
            ProjectSidebarTest.cpp
            LeftDockTest.cpp
            VcsPanelTest.cpp
            VcsDiffPreviewTest.cpp
            VimMotionTest.cpp
            VimTextObjectTest.cpp
            VimSurroundTest.cpp
            VimRegistersTest.cpp
            VimExCommandTest.cpp
            VimMagicTest.cpp
            VimEngineTest.cpp
    )

if (NED_TEST_TERMINAL_INTEGRATION)
    # PtyProcessTest.cpp forkpty's real shells/processes (sh, sleep, ...) --
    # excluded from the default test run since a real controlling-tty/session
    # forkpty isn't guaranteed to behave under every sandbox/CI environment.
    # ProjectSwitchIntegrationTest.cpp calls the real ActivateProjectRoot
    # against an existing directory, which has no test seam over
    # TerminalTabLauncher's real terminal detection -- on a real interactive
    # session (e.g. Konsole with its D-Bus gate enabled) this genuinely opens
    # a new terminal tab pointed at "/tmp" on every run.
    # Configure with -DNED_TEST_TERMINAL_INTEGRATION=ON to include both.
    target_sources(ned_tests PRIVATE
            PtyProcessTest.cpp
            ProjectSwitchIntegrationTest.cpp)
endif()

target_link_libraries(ned_tests PRIVATE ned_lib Catch2::Catch2WithMain)
# The tests read the bundled languages/plugins from build/share/ned
# (Editor/DataDir.h's executable-relative rule), assembled by ned_data and
# the compiled tables ned_tables puts beside them.
add_dependencies(ned_tests ned_data ned_tables)

# The out-of-tree language package the LanguagePackage tests load: its
# scanner is a real shared library built against Editor/Parse/Scanner.h
# alone, the way a user's would be.
add_library(ned_demo_scanner SHARED LanguagePackage/DemoScanner.cpp)
target_include_directories(ned_demo_scanner PRIVATE "${CMAKE_SOURCE_DIR}/Source")
set_target_properties(ned_demo_scanner PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_dependencies(ned_tests ned_demo_scanner)
target_compile_definitions(ned_tests PRIVATE NED_DEMO_SCANNER_LIBRARY="$<TARGET_FILE:ned_demo_scanner>")

# ThemeKeyDocsTest holds Docs/Themes.md's key reference against the real
# ui::ThemeKeys() table, so it needs to find the repo from a test binary
# that runs out of the build tree. Tests-only, deliberately: nothing in
# ned_lib or the ned binary may depend on where the source happens to be.
target_compile_definitions(ned_tests PRIVATE NED_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# SKIP_REGULAR_EXPRESSION: Catch2's SKIP() (e.g. ModeOverridesTest.cpp's
# real-system-queries tests, which SKIP rather than fail when a machine has
# no system-wide tree-sitter queries installed)
# prints "SKIPPED:" but CTest has no built-in notion of a Catch2 skip -- left
# unset, ctest classifies a fully-skipped test case as a plain failure (the
# process still returns non-zero: 0 assertions ran). Without this, any
# machine other than one with the author's own system grammar installed
# reports false failures for those tests instead of the intended skip.
# Split into two discovery calls (TEST_SPEC partitions the set, so nothing
# is registered twice) purely to attach a real CTest LABEL to [Performance]
# tests -- Catch2 tags aren't otherwise visible to CTest at all, and CI needs
# a robust `ctest -LE Performance` rather than a hand-maintained test-name
# list that goes stale the next time someone adds one. See
# CMake/Dependencies.cmake's sanitize-preset comment / .github/workflows/
# build-test.yml for why: a [Performance] budget calibrated for an optimized
# NDEBUG build is expected noise under ASan/UBSan plus a contended runner,
# not a correctness signal -- the sanitize CI job excludes this label,
# default runs it.
catch_discover_tests(ned_tests
        TEST_SPEC "~[Performance]"
        PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
catch_discover_tests(ned_tests
        TEST_SPEC "[Performance]"
        PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" LABELS "Performance")

# The generator is deterministic, so a hash per language is a complete gate
# on generator changes: a refactor meant to preserve behaviour either
# reproduces every bundled grammar's tables or it does not. This hashes the
# tables ned_tables already compiled into the data tree, so it costs nothing
# beyond the build that produced them. Tools/grammar-tables.py's own header
# comment covers re-recording GrammarTables.sha256 when a grammar or the
# table format genuinely moves. Python is an optional tool dependency, not a
# build one -- without an interpreter the gate simply isn't registered.
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME "Every bundled grammar's tables match the recorded manifest"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/grammar-tables.py" check
                    --tables "${NED_DATA_TREE}/languages"
                    --manifest "${CMAKE_CURRENT_SOURCE_DIR}/GrammarTables.sha256")
    set_tests_properties("Every bundled grammar's tables match the recorded manifest"
            PROPERTIES LABELS "GrammarTables")
    # The same interpreter gates the scanners against their grammars: a
    # ported scanner's TokenType enum and its grammar's :externals are two
    # copies of one vocabulary, and a scanner that reads a token the grammar
    # dropped reads past the external-lex-state row the engine hands it.
    # Source-only, so it needs no build output.
    add_test(NAME "Every bundled scanner indexes inside its grammar's externals"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/scanner-externals.py" check)
    set_tests_properties("Every bundled scanner indexes inside its grammar's externals"
            PROPERTIES LABELS "GrammarTables")
else()
    message(STATUS "No Python3 interpreter: skipping the grammar tables manifest test")
endif()
