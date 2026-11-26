# Writes OUT = #define GTA2_GIT "<git describe>" for the log's head; rewrites it only on a change.
execute_process(COMMAND git describe --always --dirty --abbrev=7 WORKING_DIRECTORY ${SRC}
                OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT id)
    set(id "unknown")
endif()
set(text "#define GTA2_GIT \"${id}\"\n")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT "${old}" STREQUAL "${text}")
    file(WRITE ${OUT} "${text}")
endif()
