cmake_minimum_required(VERSION 3.16)
project(Captura VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------------------
#  Options
# -------------------------------------------------------------------------
option(CAPTURA_LOG           "Enable Captura logging"                ON)
option(CAPTURA_BUILD_STL     "Build the STL (ofstream) logger"       ON)
option(CAPTURA_BUILD_SYSCALL "Build the syscall-interceptor logger"  ON)

# -------------------------------------------------------------------------
#  Core — INTERFACE target (header-only, no sources)
# -------------------------------------------------------------------------
add_library(captura_core INTERFACE)

target_include_directories(captura_core
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/captura>
)

target_compile_definitions(captura_core
        INTERFACE
        $<$<BOOL:${CAPTURA_LOG}>:CAPTURA_LOG>
)

add_library(captura::core ALIAS captura_core)

# -------------------------------------------------------------------------
#  STL logger — INTERFACE target
# -------------------------------------------------------------------------
if(CAPTURA_BUILD_STL)
    add_library(captura_stl INTERFACE)
    target_link_libraries(captura_stl INTERFACE captura_core)
    target_include_directories(captura_stl
            INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/captura>
    )
    add_library(captura::stl ALIAS captura_stl)
endif()

# -------------------------------------------------------------------------
#  Syscall logger — INTERFACE target
# -------------------------------------------------------------------------
if(CAPTURA_BUILD_SYSCALL)
    add_library(captura_syscall INTERFACE)
    target_link_libraries(captura_syscall INTERFACE captura_core)
    target_include_directories(captura_syscall
            INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/captura>
    )
    target_compile_definitions(captura_syscall
            INTERFACE __CAPTURA_POSIX
    )
    add_library(captura::syscall ALIAS captura_syscall)
endif()

# -------------------------------------------------------------------------
#  Guard
# -------------------------------------------------------------------------
if(CAPTURA_LOG AND NOT CAPTURA_BUILD_STL AND NOT CAPTURA_BUILD_SYSCALL)
    message(WARNING
            "CAPTURA_LOG is ON but no logger backend is enabled. "
            "Set CAPTURA_BUILD_STL=ON or CAPTURA_BUILD_SYSCALL=ON.")
endif()

# -------------------------------------------------------------------------
#  Install rules
# -------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_captura_install_targets captura_core)
if(CAPTURA_BUILD_STL)
    list(APPEND _captura_install_targets captura_stl)
endif()
if(CAPTURA_BUILD_SYSCALL)
    list(APPEND _captura_install_targets captura_syscall)
endif()

install(
        TARGETS   ${_captura_install_targets}
        EXPORT    CapturaTargets
)

install(
        DIRECTORY captura/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/captura
)

install(
        EXPORT    CapturaTargets
        FILE      CapturaTargets.cmake
        NAMESPACE captura::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Captura
)

set(_captura_config_content
        "@PACKAGE_INIT@
include(\"\${CMAKE_CURRENT_LIST_DIR}/CapturaTargets.cmake\")
check_required_components(Captura)
")

file(WRITE
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfig.cmake.in"
        "${_captura_config_content}"
)

configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Captura"
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CapturaConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Captura"
)