# Include a system directory (which suppresses its warnings).
function(target_include_system_directories target)
  set(multiValueArgs INTERFACE PUBLIC PRIVATE)
  cmake_parse_arguments(
    ARG
    ""
    ""
    "${multiValueArgs}"
    ${ARGN})

  foreach(scope IN ITEMS INTERFACE PUBLIC PRIVATE)
    foreach(lib_include_dirs IN LISTS ARG_${scope})
      if(NOT MSVC)
        # system includes do not work in MSVC
        # awaiting https://gitlab.kitware.com/cmake/cmake/-/issues/18272#
        # awaiting https://gitlab.kitware.com/cmake/cmake/-/issues/17904
        set(_SYSTEM SYSTEM)
      endif()
      if(${scope} STREQUAL "INTERFACE" OR ${scope} STREQUAL "PUBLIC")
        target_include_directories(
          ${target}
          ${_SYSTEM}
          ${scope}
          "$<BUILD_INTERFACE:${lib_include_dirs}>"
          "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
      else()
        target_include_directories(
          ${target}
          ${_SYSTEM}
          ${scope}
          ${lib_include_dirs})
      endif()
    endforeach()
  endforeach()

endfunction()

function(target_resolve_alias outvar lib)
  set(resolved_target "${lib}")

  get_target_property(aliased_target "${lib}" ALIASED_TARGET)
  if(aliased_target)
    set(resolved_target "${aliased_target}")
  endif()

  set(${outvar} "${resolved_target}" PARENT_SCOPE)
endfunction()

function(target_get_normalized_interface_include_dirs outvar lib)
  target_resolve_alias(resolved_target "${lib}")

  get_target_property(raw_include_dirs "${resolved_target}" INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(target_source_dir "${resolved_target}" SOURCE_DIR)
  get_target_property(target_binary_dir "${resolved_target}" BINARY_DIR)

  set(normalized_include_dirs "")

  foreach(include_dir IN LISTS raw_include_dirs)
    if(IS_ABSOLUTE "${include_dir}" OR include_dir MATCHES "^\\$<")
      list(APPEND normalized_include_dirs "${include_dir}")
    elseif(target_source_dir AND EXISTS "${target_source_dir}/${include_dir}")
      list(APPEND normalized_include_dirs "${target_source_dir}/${include_dir}")
    elseif(target_binary_dir AND EXISTS "${target_binary_dir}/${include_dir}")
      list(APPEND normalized_include_dirs "${target_binary_dir}/${include_dir}")
    else()
      list(APPEND normalized_include_dirs "${include_dir}")
    endif()
  endforeach()

  set(${outvar} "${normalized_include_dirs}" PARENT_SCOPE)
endfunction()

# Include the directories of a library target as system directories (which suppresses their warnings).
function(
  target_include_system_library
  target
  scope
  lib)
  # check if this is a target
  if(TARGET ${lib})
    target_get_normalized_interface_include_dirs(lib_include_dirs "${lib}")
    if(lib_include_dirs)
      target_include_system_directories(${target} ${scope} ${lib_include_dirs})
    else()
      message(TRACE "${lib} library does not have the INTERFACE_INCLUDE_DIRECTORIES property.")
    endif()
  endif()
endfunction()

# Link a library target as a system library (which suppresses its warnings).
function(
  target_link_system_library
  target
  scope
  lib)
  # Include the directories in the library
  target_include_system_library(${target} ${scope} ${lib})

  # Link the library
  target_link_libraries(${target} ${scope} ${lib})
endfunction()

# Link multiple library targets as system libraries (which suppresses their warnings).
function(target_link_system_libraries target)
  set(multiValueArgs INTERFACE PUBLIC PRIVATE)
  cmake_parse_arguments(
    ARG
    ""
    ""
    "${multiValueArgs}"
    ${ARGN})

  foreach(scope IN ITEMS INTERFACE PUBLIC PRIVATE)
    foreach(lib IN LISTS ARG_${scope})
      target_link_system_library(${target} ${scope} ${lib})
    endforeach()
  endforeach()
endfunction()
