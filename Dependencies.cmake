include(cmake/CPM.cmake)
set(CPM_USE_LOCAL_PACKAGES OFF)
set(CPM_SOURCE_CACHE=$HOME/.cache/CPM)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(opencattus_setup_dependencies)

  # For each dependency, see if it's
  # already been provided to us by a parent project

  if(NOT (TARGET Boost::headers OR TARGET Boost::system OR TARGET Boost::thread))
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME Boost)
    else()
      CPMAddPackage(
        NAME Boost
        VERSION 1.82.0
        GITHUB_REPOSITORY "boostorg/boost"
        GIT_TAG "boost-1.82.0"
      )
    endif()
  endif()
  
  if(NOT TARGET fmtlib::fmtlib)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME fmt)
    else()
      CPMAddPackage("gh:fmtlib/fmt#9.1.0")
    endif()
  endif()

  if(NOT TARGET spdlog::spdlog)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME spdlog
       OPTIONS
        "SPDLOG_FMT_EXTERNAL ON")
    else()
      CPMAddPackage(
        NAME
        spdlog
        VERSION
        1.11.0
        GITHUB_REPOSITORY
        "gabime/spdlog"
        OPTIONS
        "SPDLOG_FMT_EXTERNAL ON")
    endif()
  endif()

  if(NOT TARGET magic_enum::magic_enum)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME magic_enum)
    else()
      CPMAddPackage(
        NAME magic_enum
        VERSION 0.9.3
        GITHUB_REPOSITORY Neargye/magic_enum
        #OPTIONS "MAGIC_ENUM_OPT_INSTALL YES"
      )
    endif()
  endif()

  if(NOT TARGET gsl-lite::gsl-lite)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME gsl-lite)
    else()
      CPMAddPackage("gh:gsl-lite/gsl-lite@0.41.0")
    endif()
  endif()

  if(NOT TARGET CLI11::CLI11)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME CLI11)
    else()
      CPMAddPackage("gh:CLIUtils/CLI11@2.3.2")
    endif()
  endif()

  if(NOT TARGET doctest::doctest)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME doctest)
    else()
      CPMAddPackage("gh:doctest/doctest@2.4.11")
    endif()
  endif()

  if(NOT TARGET SDBusCpp::sdbus-c++)
    if (opencattus_ENABLE_CONAN)
      CPMFindPackage(NAME sdbus-c++)
    else()
      CPMAddPackage(
       VERSION 2.0.0
       GITHUB_REPOSITORY
       "Kistler-Group/sdbus-cpp")
    endif()
  endif()

  # Standalone packages
  include(FindPackageHandleStandardArgs)

  # Include module path for packages that we need to find or build
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
  include(cmake/Findnewt.cmake)
  if(NOT TARGET newt)
    CPMFindPackage(NAME newt)
  endif()

  if(NOT TARGET glibmm)
    # Using pkg_check_modules to link against the host glibmm
    # instead of using conan
    set(GLIBMM_PKG_NAMES glibmm-2.68 glibmm-2.4)
    unset(GLIBMM_FOUND)
    foreach(GLIBMM_PKG_NAME IN LISTS GLIBMM_PKG_NAMES)
      pkg_check_modules(GLIBMM QUIET ${GLIBMM_PKG_NAME})
      if(GLIBMM_FOUND)
        break()
      endif()
    endforeach()

    if(NOT GLIBMM_FOUND)
      message(FATAL_ERROR "Could not find a supported glibmm pkg-config package")
    endif()

    message(STATUS "GLIBMM_LIBRARIES=${GLIBMM_LIBRARIES}")
    message(STATUS "GLIBMM_INCLUDE_DIRS=${GLIBMM_INCLUDE_DIRS}")
    message(STATUS "GLIBMM_PKG_NAME=${GLIBMM_PKG_NAME}")
    find_package_handle_standard_args(glibmm
      DEFAULT_MSG
      GLIBMM_LIBRARIES
      GLIBMM_INCLUDE_DIRS)

    mark_as_advanced(GLIBMM_INCLUDE_DIRS GLIBMM_LIBRARIES)
  endif()

  # Set the variable ${STDC++FS} to the correct library
  include(cmake/Addstdcppfs.cmake)

endfunction()
