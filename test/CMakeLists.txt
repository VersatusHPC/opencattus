cmake_minimum_required(VERSION 3.26.5)

project(OpenCATTUS-tests LANGUAGES CXX)

# ---- Test as standalone project the exported config package ----
if (PROJECT_IS_TOP_LEVEL OR TEST_INSTALLED_VERSION)
    enable_testing()

    find_package(opencattus CONFIG REQUIRED) # for intro, project_options, ...

    if (NOT TARGET opencattus_options)
        message(FATAL_ERROR "Required config package not found!")
        return() # be strictly paranoid for Template Janitor github action! CK
    endif ()
endif ()

# Provide a simple smoke test to make sure that the CLI works and can display a --help message
# add_test(NAME cli.has_help COMMAND main --help)

# Provide a test to verify that the version being reported from the application
# matches the version given to CMake. This will be important once you package
# your program. Real world shows that this is the kind of simple mistake that is easy
# to make, but also easy to test for.
# add_test(NAME cli.version_matches COMMAND main --version)
# set_tests_properties(cli.version_matches PROPERTIES PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(REMOVE_ITEM TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
message(STATUS "Dedicated test files found are: ${TEST_SOURCES}")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp)
message(STATUS "Source files found are: ${SOURCES}")

add_library(opencattus_test_object OBJECT ${TEST_SOURCES} ${SOURCES})
add_executable(${PROJECT_NAME} main.cpp)
add_dependencies(${PROJECT_NAME} opencattus)

# Enable BUILD_TESTING macro to compile the test shipped on the source files
target_compile_definitions(opencattus_test_object PRIVATE BUILD_TESTING=1)

include(../cmake/CommonLibraries.cmake)

target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE
        opencattus_test_object
        opencattus::opencattus_warnings
        opencattus::opencattus_options)

target_link_system_libraries(
        ${PROJECT_NAME}
        PRIVATE
        ${COMMON_LIBS})

# Include glibmm headers as SYSTEM header to avoid warnings
target_include_directories(opencattus_test_object SYSTEM PRIVATE ${GLIBMM_INCLUDE_DIRS})

target_include_directories(${PROJECT_NAME} ${WARNING_GUARD} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/../include>)

target_link_libraries(
        opencattus_test_object
        PRIVATE
        opencattus::opencattus_options
        opencattus::opencattus_warnings)

target_link_system_libraries(
        opencattus_test_object
        PRIVATE
        ${COMMON_LIBS})

target_include_directories(opencattus_test_object PRIVATE
        "${CMAKE_BINARY_DIR}/../configured_files/include")
target_include_directories(opencattus_test_object ${WARNING_GUARD} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/../include>)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

if (WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(
            TARGET ${PROJECT_NAME}
            PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:tests> $<TARGET_FILE_DIR:tests>
            COMMAND_EXPAND_LISTS)
endif ()

# ---- Add Tests ----

# Register the doctest runner explicitly. The source tree ships fixture files
# under `test/sample` and repository configs under `repos/`, so the test binary
# needs to run from the repo root instead of the build directory.
add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME} --test-case-exclude=*slow*)
set_tests_properties(
        ${PROJECT_NAME}
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(
        NAME ${PROJECT_NAME}.slow
        COMMAND ${PROJECT_NAME} --test-case=*slow*)
set_tests_properties(
        ${PROJECT_NAME}.slow
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        LABELS slow
        DISABLED TRUE)

add_test(
        NAME ${PROJECT_NAME}.cli_dump_answerfile
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/files/check-dump-answerfile.sh" $<TARGET_FILE:opencattus>)
set_tests_properties(
        ${PROJECT_NAME}.cli_dump_answerfile
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(
        NAME ${PROJECT_NAME}.cli_controlled_errors
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/files/check-controlled-errors.sh" $<TARGET_FILE:opencattus>)
set_tests_properties(
        ${PROJECT_NAME}.cli_controlled_errors
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---- code coverage ----

if (ENABLE_TEST_COVERAGE)
    target_compile_options(${PROJECT_NAME} PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(${PROJECT_NAME} PUBLIC -fprofile-arcs -ftest-coverage)
endif ()
