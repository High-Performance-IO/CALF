cmake_minimum_required(VERSION 3.16)
project(CALF VERSION 0.3.0 LANGUAGES CXX)

# -------------------------------------------------------------------------
#  Options & Defaults
# -------------------------------------------------------------------------
option(CALF_LOG "Enable CALF logging globally by default" ON)
option(CALF_TESTS "Enable CALF unit tests" OFF)
option(CALF_PYTHON_TESTS "Enable CALF Python binding tests" ${CALF_TESTS})
option(CALF_BUILD_PYTHON_BINDINGS "Build Python bindings for CALF" OFF)
option(CALF_PROTOBUF "Build the Google Protobuf C++ logging component. Fallback to JSON logging when turned OFF" ON)
option(CALF_PROTOBUF_FETCH "Fetch Protobuf when a compatible installation is unavailable" ON)
option(CALF_PROTOBUF_REGENERATE "Build protoc and add the calf_regenerate_protobuf target" OFF)

set(CALF_DEFAULT_COMPONENT_NAME "calf" CACHE STRING "Fallback component name")
set(CALF_DEFAULT_LOG_DIR_NAME "./calf_logs" CACHE STRING "Fallback default log dir name")

add_library(calf_headers INTERFACE)
add_library(calf::stl ALIAS calf_headers)
add_library(calf::syscall ALIAS calf_headers)
target_include_directories(calf_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
if(CALF_LOG)
    target_compile_definitions(calf_headers INTERFACE CALF_LOG)
endif()

# ---------------------------------------------------------------------------
#  Protobuf component
# ---------------------------------------------------------------------------
if(CALF_PROTOBUF)
    # Protobuf 6.31.1 is built from upstream release 31.1, whose CMake package
    # reports the project version as 31.1.0.
    find_package(Protobuf 31.1.0 EXACT CONFIG QUIET)
    if(NOT Protobuf_FOUND)
        if(NOT CALF_PROTOBUF_FETCH)
            message(FATAL_ERROR "Protobuf 6.31.1 is required when CALF_PROTOBUF_FETCH=OFF")
        endif()
        include(FetchContent)
        set(CMAKE_CXX_STANDARD 17)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        if(DEFINED BUILD_TESTING)
            set(_CALF_SAVED_BUILD_TESTING "${BUILD_TESTING}")
            set(_CALF_BUILD_TESTING_WAS_DEFINED ON)
        endif()
        set(BUILD_TESTING OFF)
        set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_PROTOC_BINARIES ${CALF_PROTOBUF_REGENERATE} CACHE BOOL "" FORCE)
        set(protobuf_BUILD_LIBPROTOC ${CALF_PROTOBUF_REGENERATE} CACHE BOOL "" FORCE)
        set(protobuf_BUILD_LIBUPB ${CALF_PROTOBUF_REGENERATE} CACHE BOOL "" FORCE)
        set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
        set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
        set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
        set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ABSL_BUILD_TEST_HELPERS OFF CACHE BOOL "" FORCE)
        set(ABSL_IDE_FOLDER "ThirdParty/Abseil" CACHE STRING "" FORCE)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
            set(_CALF_PROTOBUF_SYSTEM SYSTEM)
        endif()
        FetchContent_Declare(
                protobuf
                GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
                GIT_TAG v31.1
                GIT_SHALLOW TRUE
                ${_CALF_PROTOBUF_SYSTEM}
        )
        unset(_CALF_PROTOBUF_SYSTEM)
        # Parent projects may enable -Werror globally. Do not apply that policy to
        # third-party sources, which use supported compiler extensions such as __int128.
        get_directory_property(_CALF_SAVED_COMPILE_OPTIONS COMPILE_OPTIONS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            add_compile_options(-Wno-error)
        endif()
        FetchContent_MakeAvailable(protobuf)
        set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_CALF_SAVED_COMPILE_OPTIONS}")
        unset(_CALF_SAVED_COMPILE_OPTIONS)
        if(_CALF_BUILD_TESTING_WAS_DEFINED)
            set(BUILD_TESTING "${_CALF_SAVED_BUILD_TESTING}")
        else()
            unset(BUILD_TESTING)
        endif()
        unset(_CALF_SAVED_BUILD_TESTING)
        unset(_CALF_BUILD_TESTING_WAS_DEFINED)
    else()
        message(STATUS "CALF using installed Protobuf ${Protobuf_VERSION}: ${Protobuf_LIBRARIES}")
    endif()

    add_library(calf_protobuf_schema STATIC
            calf/protobuf/calf_trace.pb.cc
            calf/protobuf/calf_trace.pb.h
    )
    add_library(calf_protobuf INTERFACE)
    add_library(calf::protobuf ALIAS calf_protobuf)
    set_target_properties(calf_protobuf_schema PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(calf_protobuf_schema PUBLIC cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(calf_protobuf_schema PRIVATE -Wno-error)
    endif()
    target_include_directories(calf_protobuf_schema
            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(calf_protobuf_schema PUBLIC protobuf::libprotobuf)
    target_compile_definitions(calf_protobuf INTERFACE CALF_LOG_FORMAT_PROTOBUF)
    target_link_libraries(calf_protobuf INTERFACE calf_headers calf_protobuf_schema)
    foreach(_CALF_PROTOBUF_TARGET libprotobuf libprotobuf-lite)
        if(TARGET ${_CALF_PROTOBUF_TARGET})
            set_property(TARGET ${_CALF_PROTOBUF_TARGET} PROPERTY FOLDER "ThirdParty/Protobuf")
        endif()
    endforeach()

    if(CALF_PROTOBUF_REGENERATE)
        if(NOT TARGET protobuf::protoc)
            message(FATAL_ERROR "CALF_PROTOBUF_REGENERATE requires the protobuf::protoc target")
        endif()
        add_custom_target(calf_regenerate_protobuf
                COMMAND protobuf::protoc
                        --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}
                        --python_out=${CMAKE_CURRENT_SOURCE_DIR}/bindings
                        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/calf/protobuf/calf_trace.proto
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/calf/protobuf/calf_trace.proto protobuf::protoc
                COMMENT "Regenerating CALF protobuf C++ and Python sources"
                VERBATIM
        )
    endif()
endif()

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
    if (${ARGC} GREATER 3)
        message(FATAL_ERROR "calf_enable_log accepts target, ENABLE, and optional JSON or PROTOBUF format")
    endif()

    if (ENABLE)
        target_compile_definitions(${target} PRIVATE CALF_LOG)
        if(${ARGC} EQUAL 3)
            string(TOUPPER "${ARGV2}" format)
        elseif(CALF_PROTOBUF)
            set(format "PROTOBUF")
        else()
            set(format "JSON")
        endif()

        if(format STREQUAL "PROTOBUF")
            if(NOT TARGET calf::protobuf)
                message(FATAL_ERROR "calf_enable_log(${target}) requested PROTOBUF but CALF_PROTOBUF is OFF")
            endif()
            target_link_libraries(${target} PRIVATE calf::protobuf)
        elseif(format STREQUAL "JSON")
            target_link_libraries(${target} PRIVATE calf_headers)
        else()
            message(FATAL_ERROR "calf_enable_log format must be JSON or PROTOBUF, got: ${ARGV2}")
        endif()
    endif ()
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    message(STATUS "calf_enable_log(${target}) -> ${format} include dir: ${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
endfunction()

function(calf_enable_warnings_as_errors target)
    target_compile_options(${target} PRIVATE -Werror)
endfunction()


# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------
if(CALF_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.12.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
    FetchContent_MakeAvailable(nlohmann_json)

    enable_testing()
    include(GoogleTest)

    add_executable(calf_core_tests tests/core_tests.cpp)
    calf_enable_log(calf_core_tests ON JSON)
    calf_enable_warnings_as_errors(calf_core_tests)
    target_compile_features(calf_core_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_core_tests PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
    gtest_discover_tests(calf_core_tests)

    add_executable(calf_stl_tests tests/stl_tests.cpp)
    calf_enable_log(calf_stl_tests ON JSON)
    calf_enable_warnings_as_errors(calf_stl_tests)
    target_compile_features(calf_stl_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_stl_tests PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
    gtest_discover_tests(calf_stl_tests)

    if(CALF_PROTOBUF)
        add_executable(calf_protobuf_tests tests/protobuf_tests.cpp)
        calf_enable_log(calf_protobuf_tests ON)
        calf_enable_warnings_as_errors(calf_protobuf_tests)
        target_compile_features(calf_protobuf_tests PRIVATE cxx_std_17)
        target_link_libraries(calf_protobuf_tests PRIVATE GTest::gtest_main)
        gtest_discover_tests(calf_protobuf_tests)

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(calf_syscall_protobuf_tests tests/syscall_protobuf_tests.cpp)
            calf_enable_log(calf_syscall_protobuf_tests ON)
            calf_enable_warnings_as_errors(calf_syscall_protobuf_tests)
            target_compile_definitions(calf_syscall_protobuf_tests PRIVATE __CALF_POSIX)
            target_compile_features(calf_syscall_protobuf_tests PRIVATE cxx_std_17)
            target_link_libraries(calf_syscall_protobuf_tests PRIVATE GTest::gtest_main)
            gtest_discover_tests(calf_syscall_protobuf_tests)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(calf_syscall_tests tests/syscall_tests.cpp)
        calf_enable_log(calf_syscall_tests ON JSON)
        calf_enable_warnings_as_errors(calf_syscall_tests)
        target_compile_definitions(calf_syscall_tests PRIVATE __CALF_POSIX)
        target_compile_features(calf_syscall_tests PRIVATE cxx_std_17)
        target_link_libraries(calf_syscall_tests PRIVATE GTest::gtest_main
                                                                 nlohmann_json::nlohmann_json)
        gtest_discover_tests(calf_syscall_tests)
    endif()

    add_executable(calf_disabled_tests tests/disabled_tests.cpp)
    calf_enable_log(calf_disabled_tests OFF)
    calf_enable_warnings_as_errors(calf_disabled_tests)
    target_compile_features(calf_disabled_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_disabled_tests PRIVATE GTest::gtest_main)
    gtest_discover_tests(calf_disabled_tests)
endif ()

# ---------------------------------------------------------------------------
#  Python bindings
# ---------------------------------------------------------------------------
if(CALF_BUILD_PYTHON_BINDINGS OR CALF_PYTHON_TESTS)
    include(FetchContent)
    find_package(Python 3.10 COMPONENTS Interpreter Development.Module REQUIRED)

    FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v3.0.1
    )
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(_py_calf bindings/python_bindings.cpp)
    target_include_directories(_py_calf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(_py_calf PRIVATE cxx_std_17)
    calf_set_component(_py_calf)
    calf_set_default_log_dir(_py_calf)
    calf_enable_warnings_as_errors(_py_calf)

    if(CALF_BUILD_PYTHON_BINDINGS)
        install(TARGETS _py_calf LIBRARY DESTINATION calf)
    endif()

    if(CALF_PYTHON_TESTS)
        if(NOT CALF_TESTS)
            enable_testing()
        endif()

        if(WIN32)
            set(CALF_TEST_PYTHON ${CMAKE_CURRENT_BINARY_DIR}/calf-python-test-env/Scripts/python.exe)
        else()
            set(CALF_TEST_PYTHON ${CMAKE_CURRENT_BINARY_DIR}/calf-python-test-env/bin/python)
        endif()

        add_custom_target(
                calf_python_tests
                COMMAND ${Python_EXECUTABLE} -m venv
                        ${CMAKE_CURRENT_BINARY_DIR}/calf-python-test-env
                COMMAND ${CALF_TEST_PYTHON} -m pip install "${CMAKE_CURRENT_SOURCE_DIR}[test]"
                COMMAND ${CALF_TEST_PYTHON} -m pytest -v
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_bindings_tests.py
                USES_TERMINAL
        )
        add_test(
                NAME calf_python_bindings_tests
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}
                        --target calf_python_tests
        )
    endif()
endif ()
