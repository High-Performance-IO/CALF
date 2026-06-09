cmake_minimum_required(VERSION 3.16)
project(CALF VERSION 1.0.0 LANGUAGES CXX)

# -------------------------------------------------------------------------
#  Options & Defaults
# -------------------------------------------------------------------------
option(CALF_LOG "Enable CALF logging" ON)

set(CALF_DEFAULT_COMPONENT_NAME "calf" CACHE STRING "Fallback component name")
set(CALF_DEFAULT_LOG_DIR_NAME "./calf_logs" CACHE STRING "Fallback default log dir name")

# Initialize global backing variables for the macro states
set(CALF_CURRENT_COMP_NAME "${CALF_DEFAULT_COMPONENT_NAME}" CACHE STRING "" FORCE)
set(CALF_CURRENT_LOG_DIR "${CALF_DEFAULT_LOG_DIR_NAME}" CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
#  Helper functions (Rewritten to safely modify the parent/global macro lists)
# ---------------------------------------------------------------------------
function(calf_set_component target)
    if (${ARGC} LESS 1 OR ${ARGC} GREATER 2)
        message(FATAL_ERROR "calf_set_component invoked with incorrect number of arguments")
    endif ()

    set(val "${CALF_DEFAULT_COMPONENT_NAME}")
    if(${ARGC} EQUAL 2)
        set(val "${ARGV1}")
    endif()

    set(CALF_CURRENT_COMP_NAME "${val}" CACHE STRING "" FORCE)
    calf_rebuild_macro_definitions()
endfunction()

function(calf_set_default_log_dir target)
    if (${ARGC} LESS 1 OR ${ARGC} GREATER 2)
        message(FATAL_ERROR "calf_set_default_log_dir invoked with incorrect number of arguments")
    endif ()

    set(val "${CALF_DEFAULT_LOG_DIR_NAME}")
    if(${ARGC} EQUAL 2)
        set(val "${ARGV1}")
    endif()

    set(CALF_CURRENT_LOG_DIR "${val}" CACHE STRING "" FORCE)
    calf_rebuild_macro_definitions()
endfunction()

function(calf_enable_log ENABLE)
    if (ENABLE)
        set(CALF_LOG ON CACHE BOOL "" FORCE)
    else ()
        set(CALF_LOG OFF CACHE BOOL "" FORCE)
    endif ()
    calf_rebuild_macro_definitions()
endfunction()

# -------------------------------------------------------------------------
#  Macro Collector (Exposes a single list variable to your project)
# -------------------------------------------------------------------------
macro(calf_rebuild_macro_definitions)
    set(_defs "")

    if (CALF_LOG)
        list(APPEND _defs "CALF_LOG")
    endif ()

    list(APPEND _defs "_CALF_COMPONENT_NAME=\"${CALF_CURRENT_COMP_NAME}\"")
    list(APPEND _defs "_CALF_DEFAULT_LOG_DIR_NAME=\"${CALF_CURRENT_LOG_DIR}\"")

    if (CALF_BUILD_SYSCALL)
        list(APPEND _defs "__CALF_POSIX")
    endif ()

    # Push this list straight up to your main CMake visibility scope
    set(CALF_COMPILE_DEFINITIONS "${_defs}" PARENT_SCOPE)
endmacro()

# Run initially to bootstrap the default variables
calf_rebuild_macro_definitions()