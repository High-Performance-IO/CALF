cmake_minimum_required(VERSION 3.16)
project(CALF VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------------------
#  Options
# -------------------------------------------------------------------------
option(CALF_LOG "Enable CALF logging" ON)
option(CALF_BUILD_STL "Build the STL (ofstream) logger" ON)
option(CALF_BUILD_SYSCALL "Build the syscall-interceptor logger" ON)

set(CALF_DEFAULT_COMPONENT_NAME "calf" CACHE STRING "Fallback component name when a consuming target does not set one")

# -------------------------------------------------------------------------
#  Core — INTERFACE target (header-only, no sources)
# -------------------------------------------------------------------------
add_library(calf_core INTERFACE)

target_include_directories(calf_core
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/calf>
)

target_compile_definitions(calf_core
        INTERFACE
        $<$<BOOL:${CALF_LOG}>:CALF_LOG>
        _CALF_COMPONENT_NAME="$<IF:$<BOOL:$<TARGET_PROPERTY:_CALF_COMPONENT_NAME>>,$<TARGET_PROPERTY:_CALF_COMPONENT_NAME>,${CALF_DEFAULT_COMPONENT_NAME}>"
)

add_library(calf::core ALIAS calf_core)

# ---------------------------------------------------------------------------
#  Helper: choose the component name a target logs under.
#  calf_set_component(<target> [<name>])
#     with <name>: use it verbatim
#     without:     read the CALF_COMPONENT_NAME variable from the caller's scope
# ---------------------------------------------------------------------------
function(calf_set_component target)
    if (${ARGC} LESS 1 OR ${ARGC} GREATER 2)
        message(FATAL_ERROR "calf_set_component invoked with incorrect number of arguments")
    endif()

    set_target_properties(${target} PROPERTIES _CALF_COMPONENT_NAME "${ARGV1}")
endfunction()

# -------------------------------------------------------------------------
#  STL logger — INTERFACE target
# -------------------------------------------------------------------------
if (CALF_BUILD_STL)
    add_library(calf_stl INTERFACE)
    target_link_libraries(calf_stl INTERFACE calf_core)
    target_include_directories(calf_stl
            INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/calf>
    )
    add_library(calf::stl ALIAS calf_stl)
endif ()

# -------------------------------------------------------------------------
#  Syscall logger — INTERFACE target
# -------------------------------------------------------------------------
if (CALF_BUILD_SYSCALL)
    add_library(calf_syscall INTERFACE)
    target_link_libraries(calf_syscall INTERFACE calf_core)
    target_include_directories(calf_syscall
            INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/calf>
    )
    target_compile_definitions(calf_syscall
            INTERFACE __CALF_POSIX
    )
    add_library(calf::syscall ALIAS calf_syscall)
endif ()

# -------------------------------------------------------------------------
#  Guard
# -------------------------------------------------------------------------
if (CALF_LOG AND NOT CALF_BUILD_STL AND NOT CALF_BUILD_SYSCALL)
    message(WARNING
            "CALF_LOG is ON but no logger backend is enabled. "
            "Set CALF_BUILD_STL=ON or CALF_BUILD_SYSCALL=ON.")
endif ()

# -------------------------------------------------------------------------
#  Install rules
# -------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_calf_install_targets calf_core)
if (CALF_BUILD_STL)
    list(APPEND _calf_install_targets calf_stl)
endif ()
if (CALF_BUILD_SYSCALL)
    list(APPEND _calf_install_targets calf_syscall)
endif ()

install(
        TARGETS ${_calf_install_targets}
        EXPORT CalfTargets
)

install(
        DIRECTORY calf/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/calf
)

install(
        EXPORT CalfTargets
        FILE CalfTargets.cmake
        NAMESPACE calf::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Calf
)

set(_calf_config_content
        "@PACKAGE_INIT@
include(\"\${CMAKE_CURRENT_LIST_DIR}/CalfTargets.cmake\")
check_required_components(Calf)
")

file(WRITE
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfig.cmake.in"
        "${_calf_config_content}"
)

configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Calf"
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CalfConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Calf"
)