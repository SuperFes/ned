# Third-party dependencies resolved from the system package manager (never
# FetchContent -- see git log for the migration off it). Included from the
# root CMakeLists.txt before any target that links against these; every
# target/variable this file produces (notcurses-core, utf8proc, CLI11::CLI11,
# nlohmann_json::nlohmann_json, re2::re2, pcre2-8-static, vterm, the Janet_*
# pkg-config variables, Catch2::Catch2WithMain) is consumed from Source/ and
# Tests/ subdirectories added afterward.

# System Notcurses (Gentoo's dev-cpp/notcurses) rather than FetchContent: it
# now matches the exact pinned tag (v3.0.17) and its ebuild carries the same
# three input-handling patches this project used to apply itself at
# FetchContent-patch time -- legacy-terminal Ctrl+Space (a raw NUL byte
# otherwise swallowed, id 0 colliding with notcurses_get's "no input" return),
# wheel-right (SGR Cb=67, same swallowing), and bracketed-paste marker
# recognition (\x1b[200~/\x1b[201~ as two new NCKEY_* keys, which Notcurses
# has no native support for otherwise). CMake/PatchNotcurses*.cmake are kept,
# unused, as the exact record of what a notcurses upgrade -- system package or
# otherwise -- needs to keep providing; see each one's own header comment.
# Pinned to notcurses-core specifically, matching this project's previous
# from-source USE_MULTIMEDIA=none/USE_DOCTEST=OFF/etc. trim -- notcurses-core.pc
# already excludes the multimedia/doctest/ffi extras those forced off.
# real-pixel-graphics-investigation follow-up: this project was once bitten by
# a real ABI mismatch against a stale system notcurses (enum member order in
# ncblitter_e changed between 3.0.8 and the version this project pins), which
# is exactly why the tag match above matters and isn't a formality.
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses-core)
add_library(notcurses-core ALIAS PkgConfig::NOTCURSES)
#------------------------------------------------------------------------------

#--- System utf8proc -----------------------------------------------------------
pkg_check_modules(UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
add_library(utf8proc ALIAS PkgConfig::UTF8PROC)
#------------------------------------------------------------------------------

#--- CLI11 (command-line parameter handling follow-up) ------------------------
# System package (Gentoo's dev-cpp/cli11) ships a real CLI11Config.cmake, so
# find_package hands back the same CLI11::CLI11 target FetchContent used to.
find_package(CLI11 REQUIRED)
#------------------------------------------------------------------------------

#--- nlohmann/json (LSP client follow-up) --------------------------------------
# Same as CLI11 above: the system package ships nlohmann_jsonConfig.cmake,
# so find_package hands back the same nlohmann_json::nlohmann_json target.
find_package(nlohmann_json REQUIRED)
#------------------------------------------------------------------------------

#--- RE2 (internal-project-search follow-up) ------------------------------------
# The regex engine behind the internal, multi-threaded project-search backend
# that replaces the `rg` shell-out (Editor/Project/Search.cpp) -- linear-time,
# no-backtracking matching, the same engine-model philosophy `rg` itself uses.
# This used to pin FetchContent to the last tag before RE2's CMake build
# started hard-requiring Abseil, specifically to avoid pulling all of Abseil
# in as source just for regex matching. That tradeoff doesn't apply to a
# system package: Gentoo's dev-libs/re2 already depends on a prebuilt
# dev-cpp/abseil-cpp, so find_package costs nothing extra here and hands back
# the same re2::re2 target the FetchContent build produced.
# Not every distro's package ships a CMake config, though -- confirmed live:
# Ubuntu/Debian's libre2-dev installs only re2.pc, no re2Config.cmake, so
# find_package alone would fail there even with the library present. Fall
# back to pkg-config in that case rather than requiring one specific distro's
# packaging choice.
find_package(re2 QUIET)
if (NOT TARGET re2::re2)
    pkg_check_modules(RE2 REQUIRED IMPORTED_TARGET re2)
    add_library(re2::re2 ALIAS PkgConfig::RE2)
endif()
#------------------------------------------------------------------------------

#--- PCRE2 (in-file-regex follow-up) -------------------------------------------
# The regex engine behind the two user-facing "type your own regex" surfaces
# that edit buffer/file content: query-replace-regexp (Editor/QueryReplace.cpp)
# and project-replace's rewrite step (Editor/ProjectReplace.cpp), both through
# Editor/RegexPattern.h. Deliberately a second engine alongside RE2: RE2's
# linear-time model is right for project *search* (untrusted-ish patterns fanned
# out across every file) but can't do lookaround or backreferences at all --
# PCRE2 provides those plus named groups and real Unicode classes, with JIT for
# speed and a match limit as the backtracking safety net (see RegexPattern.cpp).
# System package (Gentoo's dev-libs/libpcre2) ships no CMake config, only
# pkg-config; confirmed its libpcre2-8.so still exports the pcre2_jit_* symbols
# RegexPattern.cpp relies on. Aliased to the same pcre2-8-static name the
# FetchContent build produced so the link line below needs no change.
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
add_library(pcre2-8-static ALIAS PkgConfig::PCRE2)
#------------------------------------------------------------------------------

#--- System libvterm (terminal-panel follow-up) --------------------------------
# libvterm (the VT100/xterm emulator behind Neovim's :terminal and
# emacs-vterm) ships a plain Makefile, not CMake, so this used to be a
# fetch-but-don't-configure + hand-built-static-target arrangement, including
# hand-generating the src/encoding/*.inc tables upstream's Makefile normally
# produces via a Perl script. None of that is needed against the system
# package (Gentoo's dev-libs/libvterm), which ships those tables already
# compiled into libvterm.so; pkg-config's own module name is "vterm", not
# "libvterm".
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
add_library(vterm ALIAS PkgConfig::VTERM)
#------------------------------------------------------------------------------

#--- Catch2 ---------------------------------------------------------------------
if (NED_BUILD_TESTS)
    # System package (Gentoo's dev-cpp/catch, v3.15.3) ships Catch2Config.cmake,
    # which appends its own directory to CMAKE_MODULE_PATH itself -- so
    # include(Catch) below finds its Catch.cmake module with no extra wiring,
    # the same way FetchContent's own extras/ directory used to be added by hand.
    find_package(Catch2 3 REQUIRED)
endif()
#------------------------------------------------------------------------------

#--- Janet ------------------------------------------------------------------
# Requires a system install of `janet` discoverable via pkg-config (the build
# fails at configure time otherwise). Sets Janet_INCLUDE_DIRS/Janet_LIBRARIES/
# Janet_LIBRARY_DIRS, consumed by Source/CMakeLists.txt.
pkg_check_modules(Janet REQUIRED janet)
#------------------------------------------------------------------------------
