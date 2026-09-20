# Every bundled language's parse tables, compiled from its grammar.janet by
# ned-langc into the build tree's share/ned/languages/<name>/tables, beside
# the definition and query files ned_data copied there
# (CMake/DataTree.cmake). Included from the root CMakeLists.txt after
# add_subdirectory(Source), since it runs ned-langc.
#
# A table depends on its grammar.janet and on ned-langc -- deliberately not
# on `ned`. The generator is its own library and its own program
# (Source/CMakeLists.txt's ned_grammar_compile) precisely so that this
# dependency is narrow: an edit anywhere in the editor relinks `ned` without
# invalidating a single table, where depending on `ned` made every such edit
# cost a full recompile of all of them (~2 minutes). A generator change
# still recompiles every language, which is what it should cost; the set
# builds in parallel and Tests/GrammarTables.sha256 gates the result.
#
# The install rule takes the compiled files from here: an installed prefix's
# share/ned/languages/<name>/ is the build tree's.

file(GLOB _ned_grammar_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages/*/grammar.janet")
set(_ned_table_files)
foreach(grammar IN LISTS _ned_grammar_files)
    get_filename_component(language_dir "${grammar}" DIRECTORY)
    get_filename_component(language "${language_dir}" NAME)
    set(output "${NED_DATA_TREE}/languages/${language}/tables")
    add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${NED_DATA_TREE}/languages/${language}"
            COMMAND ned-langc "${language_dir}" -o "${output}"
            DEPENDS ned-langc "${grammar}"
            COMMENT "Compiling the ${language} grammar"
            VERBATIM)
    list(APPEND _ned_table_files "${output}")
endforeach()

add_custom_target(ned_tables ALL DEPENDS ${_ned_table_files})
add_dependencies(ned_tables ned_data)

install(DIRECTORY "${NED_DATA_TREE}/languages/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ned/languages"
        FILES_MATCHING PATTERN "tables"
        PATTERN "corpus" EXCLUDE
        PATTERN "upstream" EXCLUDE)
