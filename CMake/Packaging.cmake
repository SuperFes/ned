# Binary packages through CPack (`cpack -G DEB`, `cpack -G RPM`), meant for a
# NED_BUNDLE_LIBS build: a distribution's Notcurses lacks ned's patches, and
# Debian and Ubuntu don't package Janet at all. Every other library stays
# shared, so each package is built on the release it targets.

include(GNUInstallDirs)

# The static agents (CMakePresets.json's agent-* presets) ned deploys to a
# remote host, laid out as <arch>/ned-agent. A package ships every
# architecture's, since a remote host's needn't match the editor's.
set(NED_PACKAGE_AGENTS "" CACHE PATH "Directory of <arch>/ned-agent static agents to install under libexec/ned/agent")
if (NED_PACKAGE_AGENTS)
    install(DIRECTORY "${NED_PACKAGE_AGENTS}/"
            DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/ned/agent"
            USE_SOURCE_PERMISSIONS)
endif()

install(FILES LICENSE THIRD-PARTY-LICENSES.md DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/ned")
install(FILES Packaging/ned.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES Packaging/ned.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

set(CPACK_PACKAGE_NAME ned)
set(CPACK_PACKAGE_VENDOR "Aaron Doom")
set(CPACK_PACKAGE_CONTACT "Aaron Doom <aarondoom@cookiedoom.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/SuperFes/ned")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal text editor scriptable in Janet")
set(CPACK_PACKAGE_DESCRIPTION
        "Ned is a terminal text editor written in C++23, aiming for Emacs-class feature coverage with Janet in the role Elisp plays in Emacs: buffers, commands, keymaps, modes and LSP/DAP/VCS configuration are all scriptable.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION editors)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# libexec/ned/port-scanner; RPM finds its shebang on its own.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS python3)

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP Applications/Editors)
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
# The filesystem package owns these; claiming them too conflicts with it.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/libexec /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
        /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
        /usr/share/man /usr/share/man/man1 /usr/share/man/man7)

include(CPack)
