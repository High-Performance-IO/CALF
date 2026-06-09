cmake_minimum_required(VERSION 3.16)
project(CALF VERSION 1.0.0 LANGUAGES CXX)

# -------------------------------------------------------------------------
#  Options & Defaults
# -------------------------------------------------------------------------
option(CALF_LOG "Enable CALF logging globally by default" ON)

set(CALF_DEFAULT_COMPONENT_NAME "calf" CACHE STRING "Fallback component name")
set(CALF_DEFAULT_LOG_DIR_NAME "./calf_logs" CACHE STRING "Fallback default log dir name")

# ---------------------------------------------------------------------------
#  Helper functions (Now modifying targets directly)
# ---------------------------------------------------------------------------

function(calf_set_component target)
    if (${ARGC} LESS 1 OR ${ARGC} GREATER 2)
        message(FATAL_ERROR "calf_set_component invoked with incorrect number of arguments")
    endif ()

    set(val "${CALF_DEFAULT_COMPONENT_NAME}")
    if(${ARGC} EQUAL 2)
        set(val "${ARGV1}")
    endif()

    # Apply directly to the target scope
    target_compile_definitions(${target} PRIVATE _CALF_COMPONENT_NAME="${val}")
endfunction()

function(calf_set_default_log_dir target)
    if (${ARGC} LESS 1 OR ${ARGC} GREATER 2)
        message(FATAL_ERROR "calf_set_default_log_dir invoked with incorrect number of arguments")
    endif ()

    set(val "${CALF_DEFAULT_LOG_DIR_NAME}")
    if(${ARGC} EQUAL 2)
        set(val "${ARGV1}")
    endif()

    # Apply directly to the target scope
    target_compile_definitions(${target} PRIVATE _CALF_DEFAULT_LOG_DIR_NAME="${val}")
endfunction()

function(calf_enable_log target ENABLE)
    if (ENABLE)
        target_compile_definitions(${target} PRIVATE CALF_LOG)
    endif ()
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
endfunction()