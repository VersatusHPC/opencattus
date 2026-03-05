# Add all source files except main.cpp
file(GLOB_RECURSE SOURCE_FILES *.cpp)
file(GLOB PRESENTER_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/presenter/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp)
list(REMOVE_ITEM SOURCE_FILES ${PRESENTER_SOURCE_FILES})
list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

message(STATUS "Source files found are: ${SOURCE_FILES}")

include(../cmake/CommonLibraries.cmake)

# Build the presenter on its onw object, to be faster
# Not wo
add_library(opencattus_presenter_object OBJECT ${PRESENTER_SOURCE_FILES})
target_include_directories(opencattus_presenter_object PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_link_system_libraries(
        opencattus_presenter_object
        PRIVATE
        ${COMMON_LIBS})

# Build a library and a ${opencattus_BINARY_NAME} binary that links to the library
add_library(opencattus_object OBJECT ${SOURCE_FILES})

# Include glibmm headers as SYSTEM header to avoid warnings
target_include_directories(opencattus_object SYSTEM PRIVATE ${GLIBMM_INCLUDE_DIRS})

# add_library(opencattus_static STATIC $<TARGET_OBJECTS:opencattus_object>)
add_library(opencattus_static STATIC $<TARGET_OBJECTS:opencattus_object> $<TARGET_OBJECTS:opencattus_presenter_object>)
add_executable(${opencattus_BINARY_NAME} main.cpp)

target_link_libraries(
        ${opencattus_BINARY_NAME}
        PRIVATE
        opencattus_static
        opencattus::opencattus_options
        opencattus::opencattus_warnings)

target_link_system_libraries(
        ${opencattus_BINARY_NAME}
        PRIVATE
        ${COMMON_LIBS})

target_link_libraries(
        opencattus_object
        PRIVATE
        opencattus::opencattus_options
        opencattus::opencattus_warnings)

target_link_system_libraries(
        opencattus_object
        PRIVATE
        ${COMMON_LIBS})

target_include_directories(opencattus_static PRIVATE
        "${CMAKE_BINARY_DIR}/configured_files/include")
target_include_directories(opencattus_static ${WARNING_GUARD} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

target_include_directories(opencattus_object PRIVATE
        "${CMAKE_BINARY_DIR}/configured_files/include")
target_include_directories(opencattus_object ${WARNING_GUARD} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

target_include_directories(${opencattus_BINARY_NAME} PRIVATE
        "${CMAKE_BINARY_DIR}/configured_files/include")
target_include_directories(${opencattus_BINARY_NAME} ${WARNING_GUARD} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

# Set target locations to the root folder of the project in /bin and /lib.
# WARNING: This will break multiple target compile, disable it if necessary
# This is only for convenience
#set_target_properties(${opencattus_BINARY_NAME}
#    PROPERTIES
#    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib"
#    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib"
#    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
#)
