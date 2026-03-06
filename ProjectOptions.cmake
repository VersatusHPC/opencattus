include(cmake/SystemLink.cmake)
include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)

macro(opencattus_supports_sanitizers)
  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)
    set(SUPPORTS_UBSAN ON)
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    set(SUPPORTS_ASAN ON)
  endif()
endmacro()

macro(opencattus_setup_options)
  option(opencattus_ENABLE_HARDENING "Enable hardening" ON)
  option(opencattus_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    opencattus_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    opencattus_ENABLE_HARDENING
    OFF)

  if(NOT PROJECT_IS_TOP_LEVEL OR opencattus_PACKAGING_MAINTAINER_MODE)
    option(opencattus_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(opencattus_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(opencattus_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(opencattus_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(opencattus_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(opencattus_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(opencattus_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(opencattus_ENABLE_PCH "Enable precompiled headers" OFF)
    option(opencattus_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(opencattus_ENABLE_IPO "Enable IPO/LTO" ON)
    option(opencattus_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(opencattus_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(opencattus_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(opencattus_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(opencattus_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(opencattus_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(opencattus_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(opencattus_ENABLE_PCH "Enable precompiled headers" OFF)
    option(opencattus_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      opencattus_ENABLE_IPO
      opencattus_WARNINGS_AS_ERRORS
      opencattus_ENABLE_USER_LINKER
      opencattus_ENABLE_SANITIZER_ADDRESS
      opencattus_ENABLE_SANITIZER_LEAK
      opencattus_ENABLE_SANITIZER_UNDEFINED
      opencattus_ENABLE_SANITIZER_THREAD
      opencattus_ENABLE_SANITIZER_MEMORY
      opencattus_ENABLE_UNITY_BUILD
      opencattus_ENABLE_CLANG_TIDY
      opencattus_ENABLE_CPPCHECK
      opencattus_ENABLE_COVERAGE
      opencattus_ENABLE_PCH
      opencattus_ENABLE_CACHE)
  endif()

  opencattus_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED AND (opencattus_ENABLE_SANITIZER_ADDRESS OR opencattus_ENABLE_SANITIZER_THREAD OR opencattus_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(opencattus_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(opencattus_global_options)
  if(opencattus_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    opencattus_enable_ipo()
  endif()

  if(opencattus_ENABLE_HARDENING AND opencattus_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR opencattus_ENABLE_SANITIZER_UNDEFINED
       OR opencattus_ENABLE_SANITIZER_ADDRESS
       OR opencattus_ENABLE_SANITIZER_THREAD
       OR opencattus_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${opencattus_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${opencattus_ENABLE_SANITIZER_UNDEFINED}")
    opencattus_enable_hardening(opencattus_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(opencattus_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(opencattus_warnings INTERFACE)
  add_library(opencattus_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  opencattus_set_project_warnings(
    opencattus_warnings
    ${opencattus_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  if(opencattus_ENABLE_USER_LINKER)
    include(cmake/Linker.cmake)
    configure_linker(opencattus_options)
  endif()

  include(cmake/Sanitizers.cmake)
  opencattus_enable_sanitizers(
    opencattus_options
    ${opencattus_ENABLE_SANITIZER_ADDRESS}
    ${opencattus_ENABLE_SANITIZER_LEAK}
    ${opencattus_ENABLE_SANITIZER_UNDEFINED}
    ${opencattus_ENABLE_SANITIZER_THREAD}
    ${opencattus_ENABLE_SANITIZER_MEMORY})

  set_target_properties(opencattus_options PROPERTIES UNITY_BUILD ${opencattus_ENABLE_UNITY_BUILD})

  if(opencattus_ENABLE_PCH)
    target_precompile_headers(
      opencattus_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(opencattus_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    opencattus_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(opencattus_ENABLE_CLANG_TIDY)
    opencattus_enable_clang_tidy(opencattus_options ${opencattus_WARNINGS_AS_ERRORS})
  endif()

  if(opencattus_ENABLE_CPPCHECK)
    opencattus_enable_cppcheck(${opencattus_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()

  if(opencattus_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    opencattus_enable_coverage(opencattus_options)
  endif()

  if(opencattus_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(opencattus_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(opencattus_ENABLE_HARDENING AND NOT opencattus_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR opencattus_ENABLE_SANITIZER_UNDEFINED
       OR opencattus_ENABLE_SANITIZER_ADDRESS
       OR opencattus_ENABLE_SANITIZER_THREAD
       OR opencattus_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    opencattus_enable_hardening(opencattus_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
