# Run as `cmake -DSOURCE=<dir> -DDEST=<dir> -P PruneDataTree.cmake` after
# DEST has been copied from SOURCE: deletes whatever DEST holds that SOURCE
# no longer does, so a file removed from the source tree stops being loaded
# from the build tree. Language files are discovered by directory scan (an
# `upstream/<kind>.janet` layers under the package's own), so a leftover
# copy changes behaviour rather than just taking up space.
#
# The one exception is `tables`, which ned-langc compiles into the tree
# rather than being copied from SOURCE. It is kept while its grammar.janet
# still exists, so pruning never forces the grammars to recompile.

if (NOT IS_DIRECTORY "${SOURCE}" OR NOT IS_DIRECTORY "${DEST}")
    message(FATAL_ERROR "PruneDataTree: SOURCE and DEST must both be directories")
endif()

# Sorted, so a directory precedes its contents: once it is removed, the
# EXISTS check below skips everything that was inside it.
file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${DEST}" "${DEST}/*")
list(SORT entries)
foreach (entry IN LISTS entries)
    if (NOT EXISTS "${DEST}/${entry}" OR EXISTS "${SOURCE}/${entry}")
        continue()
    endif()
    get_filename_component(name "${entry}" NAME)
    get_filename_component(parent "${entry}" DIRECTORY)
    if (name STREQUAL "tables" AND EXISTS "${SOURCE}/${parent}/grammar.janet")
        continue()
    endif()
    file(REMOVE_RECURSE "${DEST}/${entry}")
endforeach()
