cmake_minimum_required(VERSION 3.16)
project(CALF VERSION 0.0.1 LANGUAGES CXX)

# -------------------------------------------------------------------------
#  Options & Defaults
# -------------------------------------------------------------------------
option(CALF_LOG "Enable CALF logging globally by default" ON)
option(CALF_TESTS "Enable CALF unit tests" OFF)
option(CALF_PYTHON_TESTS "Enable CALF Python binding tests" ${CALF_TESTS})
option(BUILD_PYTHON_BINDINGS "Build Python bindings for CALF" OFF)

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
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    message(STATUS "calf_enable_log(${target}) -> include dir: ${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
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
    calf_enable_log(calf_core_tests ON)
    target_compile_features(calf_core_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_core_tests PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
    gtest_discover_tests(calf_core_tests)

    add_executable(calf_stl_tests tests/stl_tests.cpp)
    calf_enable_log(calf_stl_tests ON)
    target_compile_features(calf_stl_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_stl_tests PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
    gtest_discover_tests(calf_stl_tests)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(calf_syscall_tests tests/syscall_tests.cpp)
        calf_enable_log(calf_syscall_tests ON)
        target_compile_definitions(calf_syscall_tests PRIVATE __CALF_POSIX)
        target_compile_features(calf_syscall_tests PRIVATE cxx_std_17)
        target_link_libraries(calf_syscall_tests PRIVATE GTest::gtest_main
                                                                 nlohmann_json::nlohmann_json)
        gtest_discover_tests(calf_syscall_tests)
    endif()

    add_executable(calf_disabled_tests tests/disabled_tests.cpp)
    calf_enable_log(calf_disabled_tests OFF)
    target_compile_features(calf_disabled_tests PRIVATE cxx_std_17)
    target_link_libraries(calf_disabled_tests PRIVATE GTest::gtest_main)
    gtest_discover_tests(calf_disabled_tests)
endif ()

# ---------------------------------------------------------------------------
#  Python bindings
# ---------------------------------------------------------------------------
if(BUILD_PYTHON_BINDINGS OR CALF_PYTHON_TESTS)
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

    if(BUILD_PYTHON_BINDINGS)
        install(TARGETS _py_calf LIBRARY DESTINATION calf)
    endif()

    if(CALF_PYTHON_TESTS)
        if(NOT CALF_TESTS)
            enable_testing()
        endif()

        add_test(
                NAME calf_python_bindings_tests
                COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_bindings_tests.py
                        $<TARGET_FILE_DIR:_py_calf>
        )
        add_custom_target(
                calf_python_tests
                COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_bindings_tests.py
                        $<TARGET_FILE_DIR:_py_calf>
                DEPENDS _py_calf
                USES_TERMINAL
        )
    endif()
endif ()
