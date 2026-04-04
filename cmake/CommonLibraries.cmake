# This file sets the required libraries for linking the project.

function(opencattus_require_target outvar)
    foreach(candidate IN LISTS ARGN)
        if(TARGET "${candidate}")
            set(${outvar} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    string(JOIN ", " candidates ${ARGN})
    message(FATAL_ERROR "Unable to locate any of the required CMake targets: ${candidates}")
endfunction()

function(opencattus_optional_target outvar)
    foreach(candidate IN LISTS ARGN)
        if(TARGET "${candidate}")
            set(${outvar} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${outvar} "" PARENT_SCOPE)
endfunction()

if(TARGET SDBusCpp::sdbus-c++)
    set(OPENCATTUS_SDBUS_TARGET SDBusCpp::sdbus-c++)
elseif(TARGET sdbus-c++)
    set(OPENCATTUS_SDBUS_TARGET sdbus-c++)
else()
    message(FATAL_ERROR "Unable to locate an sdbus-c++ CMake target")
endif()

opencattus_require_target(
    OPENCATTUS_BOOST_HEADERS_TARGET
    Boost::headers
    Boost::boost
    boost::boost)
opencattus_require_target(
    OPENCATTUS_BOOST_SYSTEM_TARGET
    Boost::system
    boost::system)
opencattus_require_target(
    OPENCATTUS_BOOST_THREAD_TARGET
    Boost::thread
    boost::thread)
opencattus_optional_target(
    OPENCATTUS_BOOST_ALGORITHM_TARGET
    Boost::algorithm
    boost_algorithm)
opencattus_optional_target(
    OPENCATTUS_BOOST_ASIO_TARGET
    Boost::asio
    boost_asio)
opencattus_optional_target(
    OPENCATTUS_BOOST_LEXICAL_CAST_TARGET
    Boost::lexical_cast
    boost_lexical_cast)
opencattus_optional_target(
    OPENCATTUS_BOOST_PROCESS_TARGET
    Boost::process
    boost_process)
opencattus_optional_target(
    OPENCATTUS_BOOST_PROPERTY_TREE_TARGET
    Boost::property_tree
    boost_property_tree)

set(COMMON_LIBS
        ${NEWT_LIBRARY}
        ${GLIBMM_LIBRARIES}
        ${STDC++FS}
        ${OPENCATTUS_BOOST_ALGORITHM_TARGET}
        ${OPENCATTUS_BOOST_ASIO_TARGET}
        ${OPENCATTUS_BOOST_HEADERS_TARGET}
        ${OPENCATTUS_BOOST_LEXICAL_CAST_TARGET}
        ${OPENCATTUS_BOOST_PROCESS_TARGET}
        ${OPENCATTUS_BOOST_PROPERTY_TREE_TARGET}
        ${OPENCATTUS_BOOST_SYSTEM_TARGET}
        ${OPENCATTUS_BOOST_THREAD_TARGET}
        CLI11::CLI11
        doctest::doctest
        fmt::fmt
        gsl::gsl-lite
        magic_enum::magic_enum
        resolv
        ${OPENCATTUS_SDBUS_TARGET}
        spdlog::spdlog
)
