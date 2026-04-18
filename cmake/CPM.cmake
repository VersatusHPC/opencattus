set(CPM_DOWNLOAD_VERSION 0.40.2)

if(CPM_SOURCE_CACHE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)
get_filename_component(CPM_DOWNLOAD_DIRECTORY "${CPM_DOWNLOAD_LOCATION}" DIRECTORY)

function(cpm_download_is_valid path output_variable)
  if(NOT EXISTS "${path}")
    set(${output_variable} FALSE PARENT_SCOPE)
    return()
  endif()

  file(SIZE "${path}" download_size)
  if(download_size EQUAL 0)
    set(${output_variable} FALSE PARENT_SCOPE)
    return()
  endif()

  file(READ "${path}" download_contents)
  if(download_contents MATCHES "CPMAddPackage")
    set(${output_variable} TRUE PARENT_SCOPE)
  else()
    set(${output_variable} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(download_cpm)
  set(cpm_url
      "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake")
  set(max_attempts 5)
  file(MAKE_DIRECTORY "${CPM_DOWNLOAD_DIRECTORY}")

  foreach(attempt RANGE 1 ${max_attempts})
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION} (attempt ${attempt}/${max_attempts})")
    file(DOWNLOAD "${cpm_url}" "${CPM_DOWNLOAD_LOCATION}" STATUS download_status)
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)

    cpm_download_is_valid("${CPM_DOWNLOAD_LOCATION}" valid_download)
    if(status_code EQUAL 0 AND valid_download)
      return()
    endif()

    file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
    if(attempt LESS max_attempts)
      execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 5)
    endif()
  endforeach()

  message(FATAL_ERROR
          "Failed to download CPM.cmake from ${cpm_url}: ${status_message}")
endfunction()

cpm_download_is_valid("${CPM_DOWNLOAD_LOCATION}" cpm_is_valid)
if(NOT cpm_is_valid)
  download_cpm()
endif()
unset(cpm_is_valid)

include(${CPM_DOWNLOAD_LOCATION})
