cmake_minimum_required(VERSION 3.16)
project(Captura VERSION 1.0.0 LANGUAGES CXX)

# -------------------------------------------------------------------------
#  Language standard
# -------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------------------
#  Options
# -------------------------------------------------------------------------
option(CAPTURA_LOG          "Enable Captura logging"                ON)
option(CAPTURA_BUILD_STL    "Build the STL (ofstream) logger"       ON)
option(CAPTURA_BUILD_SYSCALL "Build the syscall-interceptor logger" OFF)

# -------------------------------------------------------------------------
#  Core library  (BaseLogger + JsonBaseLogger — always built)
# -------------------------------------------------------------------------
add_library(captura_core STATIC
        src/BaseLogger.cpp
)

target_include_directories(captura_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/captura>
)

target_compile_definitions(captura_core
        PUBLIC
        $<$<BOOL:${CAPTURA_LOG}>:CAPTURA_LOG>
)

add_library(captura::core ALIAS captura_core)

# -------------------------------------------------------------------------
#  STL logger  (std::ofstream backend — enabled by default)
# -------------------------------------------------------------------------
if(CAPTURA_BUILD_STL)
    add_library(captura_stl STATIC
            src/StlLogger.cpp
    )
    target_link_libraries(captura_stl PUBLIC captura_core)
    target_include_directories(captura_stl
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/captura>
    )
    add_library(captura::stl ALIAS captura_stl)
endif()

# -------------------------------------------------------------------------
#  Syscall logger  (raw POSIX write — for interceptor builds)
# -------------------------------------------------------------------------
if(CAPTURA_BUILD_SYSCALL)
    add_library(captura_syscall STATIC
            src/SyscallLogger.cpp
    )
    target_link_libraries(captura_syscall PUBLIC captura_core)
    target_include_directories(captura_syscall
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/captura>
    )
    target_compile_definitions(captura_syscall
            PUBLIC __CAPTURA_POSIX
    )
    target_compile_options(captura_syscall
            PUBLIC -ftls-model=initial-exec
    )
    add_library(captura::syscall ALIAS captura_syscall)
endif()

# -------------------------------------------------------------------------
#  Guard: at least one logger backend must be enabled
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
        ARCHIVE   DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/captura
)

install(
        EXPORT    CapturaTargets
        FILE      CapturaTargets.cmake
        NAMESPACE captura::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Captura
)

# -------------------------------------------------------------------------
#  Package config
# -------------------------------------------------------------------------
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