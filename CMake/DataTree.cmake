# ned's data tree: the bundled language definitions/query files
# (Source/Languages/) and Janet plugins (Source/Janet/Plugins/), read at
# runtime from `<data dir>/languages` and `<data dir>/plugins`
# (Source/Editor/DataDir.h) -- nothing under them is compiled into the
# binary. Included from the root CMakeLists.txt before
# add_subdirectory(Source); ned and ned_tests depend on the ned_data target.
#
# The build tree's share/ned/ mirrors an installed prefix's so DataDir's
# executable-relative rule (<exe dir>/../share/ned) resolves build/Source/ned
# and build/Tests/ned_tests to it exactly the way /usr/bin/ned resolves to
# /usr/share/ned. copy_directory_if_different re-runs on every build (cheap:
# a few hundred small text files) so an edited language file is live on the
# next build without a reconfigure. The copy never deletes, so
# PruneDataTree.cmake follows it to remove what the source tree no longer has.

set(NED_DATA_TREE "${CMAKE_BINARY_DIR}/share/ned")

add_custom_target(ned_data ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages" "${NED_DATA_TREE}/languages"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins" "${NED_DATA_TREE}/plugins"
        COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages"
                "-DDEST=${NED_DATA_TREE}/languages" -P "${CMAKE_CURRENT_SOURCE_DIR}/CMake/PruneDataTree.cmake"
        COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins"
                "-DDEST=${NED_DATA_TREE}/plugins" -P "${CMAKE_CURRENT_SOURCE_DIR}/CMake/PruneDataTree.cmake"
        COMMENT "Assembling ${NED_DATA_TREE}"
        VERBATIM)

include(GNUInstallDirs)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ned/languages"
        FILES_MATCHING PATTERN "*.janet"
        PATTERN "corpus" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ned/plugins"
        FILES_MATCHING PATTERN "*.janet")
