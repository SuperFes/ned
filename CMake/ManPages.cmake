# Man pages, rendered by pandoc from the Markdown under Docs/man/.
#
# Those sources are generated, not written: Tests/InvocationReferenceTest.cpp
# and Tests/CommandReferenceTest.cpp render them from the live CLI::App,
# CommandRegistry and `ned/*` binding table, and fail the build when the
# committed copy disagrees. So a man page cannot document a flag the binary
# doesn't have -- which is the whole reason this is pandoc over generated
# Markdown rather than hand-written roff.
#
# Off by default, and never part of ALL unless asked for: pandoc is a Haskell
# toolchain most people building an editor have no reason to install, and a
# missing man page is not a broken build. A distro package turns it on.

option(NED_BUILD_MAN "Build and install man pages (requires pandoc)" OFF)

if (NOT NED_BUILD_MAN)
    return()
endif()

find_program(PANDOC_EXECUTABLE pandoc REQUIRED)
include(GNUInstallDirs)

set(NED_MAN_BUILD_DIR "${CMAKE_BINARY_DIR}/man")
file(MAKE_DIRECTORY "${NED_MAN_BUILD_DIR}")

# <name>.<section>, matching the Docs/man/<name>.<section>.md sources.
set(NED_MAN_PAGES ned.1 ned-commands.7 ned-janet.7)

set(_ned_man_outputs)
foreach (page IN LISTS NED_MAN_PAGES)
    set(_src "${CMAKE_SOURCE_DIR}/Docs/man/${page}.md")
    set(_out "${NED_MAN_BUILD_DIR}/${page}")
    # No --metadata date: pandoc defaults it to the day of the build, which
    # would make two builds of the same source produce different files.
    add_custom_command(
            OUTPUT "${_out}"
            # -smart: pandoc's typographic pass turns `--user` into an
            # en-dash, which silently corrupts every flag a description
            # mentions. -tex_math_dollars: a `$EDITOR`/`$XDG_...` pair in one
            # docstring otherwise parses as TeX math.
            COMMAND "${PANDOC_EXECUTABLE}" --standalone
                    --from "markdown-smart-tex_math_dollars-tex_math_single_backslash"
                    --to man
                    --metadata "footer=ned ${PROJECT_VERSION}"
                    --metadata "header=ned manual"
                    --output "${_out}" "${_src}"
            DEPENDS "${_src}"
            COMMENT "Generating man page ${page}"
            VERBATIM)
    list(APPEND _ned_man_outputs "${_out}")
endforeach ()

# Each argv[0] symlink gets a `.so` stub rather than a copy of ned.1 --
# man's own include directive, so `man ned-format` renders ned(1) itself and
# there is exactly one page to keep correct.
set(NED_MAN_ALIASES ned-format ned-langc ned-import-language ned-test-language)
foreach (alias IN LISTS NED_MAN_ALIASES)
    set(_out "${NED_MAN_BUILD_DIR}/${alias}.1")
    file(WRITE "${_out}" ".so man1/ned.1\n")
    list(APPEND _ned_man_outputs "${_out}")
endforeach ()

add_custom_target(ned_man ALL DEPENDS ${_ned_man_outputs})

install(FILES "${NED_MAN_BUILD_DIR}/ned.1" DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
foreach (alias IN LISTS NED_MAN_ALIASES)
    install(FILES "${NED_MAN_BUILD_DIR}/${alias}.1" DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endforeach ()
install(FILES "${NED_MAN_BUILD_DIR}/ned-commands.7" "${NED_MAN_BUILD_DIR}/ned-janet.7"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man7")
