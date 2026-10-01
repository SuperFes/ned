# Run as `cmake -DSCRIPT=<PruneDataTree.cmake> -DWORK=<scratch dir> -P ...`:
# builds a source/copy pair, prunes the copy, and checks what survived.

file(REMOVE_RECURSE "${WORK}")
set(source "${WORK}/source")
set(dest "${WORK}/dest")

foreach (path IN ITEMS
        "kept/grammar.janet" "kept/locals.janet" "kept/upstream/highlights.janet" "kept/corpus/a.txt"
        "gone/grammar.janet")
    file(WRITE "${source}/${path}" "")
endforeach()
file(REMOVE "${source}/gone/grammar.janet")
foreach (path IN ITEMS
        "kept/grammar.janet" "kept/locals.janet" "kept/tables" "kept/upstream/highlights.janet"
        "kept/upstream/locals.janet" "kept/corpus/a.txt" "kept/corpus/b.txt" "kept/scanner/scanner.c"
        "gone/grammar.janet" "gone/tables" "nogrammar/tables")
    file(WRITE "${dest}/${path}" "")
endforeach()
file(REMOVE_RECURSE "${source}/gone")
file(WRITE "${source}/nogrammar/language.janet" "")
file(WRITE "${dest}/nogrammar/language.janet" "")

execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${source}" "-DDEST=${dest}" -P "${SCRIPT}"
        RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "PruneDataTree.cmake failed: ${result}")
endif()

set(failures "")
foreach (path IN ITEMS
        "kept/grammar.janet" "kept/locals.janet" "kept/tables" "kept/upstream/highlights.janet"
        "kept/corpus/a.txt" "nogrammar/language.janet")
    if (NOT EXISTS "${dest}/${path}")
        string(APPEND failures "\n  removed ${path}")
    endif()
endforeach()
# A stale file, a stale directory, a deleted language's tables, and a
# `tables` with no grammar beside it.
foreach (path IN ITEMS
        "kept/upstream/locals.janet" "kept/corpus/b.txt" "kept/scanner" "gone" "nogrammar/tables")
    if (EXISTS "${dest}/${path}")
        string(APPEND failures "\n  kept ${path}")
    endif()
endforeach()
if (failures)
    message(FATAL_ERROR "PruneDataTree.cmake:${failures}")
endif()
file(REMOVE_RECURSE "${WORK}")
