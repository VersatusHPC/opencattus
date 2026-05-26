# A very simple example of a configured file that might need to be
# converted to one that is publicly installed in the case that
# you are developing a library
configure_file("config.hpp.in" "${CMAKE_BINARY_DIR}/configured_files/include/internal_use_only/config.hpp" ESCAPE_QUOTES)

# Generate embedded copies of the repository configuration files so the
# binary can seed /opt/opencattus/conf/repos/ on first run without relying
# on the install step. Mirrors opencattus_REPOSITORY_CONFIGS defined in the
# top-level CMakeLists.txt.
set(opencattus_EMBEDDED_REPOS_FILES
        repos.conf
        alma.conf
        rhel.conf
        oracle.conf
        rocky-upstream.conf
        rocky-vault.conf)

set(opencattus_EMBEDDED_REPOS_HEADER
        "${CMAKE_BINARY_DIR}/configured_files/include/internal_use_only/embedded_repos.hpp")

set(_opencattus_embedded_repos_inputs "")
foreach(_name IN LISTS opencattus_EMBEDDED_REPOS_FILES)
    list(APPEND _opencattus_embedded_repos_inputs
            "${PROJECT_SOURCE_DIR}/repos/${_name}")
endforeach()

add_custom_command(
        OUTPUT "${opencattus_EMBEDDED_REPOS_HEADER}"
        COMMAND ${CMAKE_COMMAND}
                "-DOPENCATTUS_REPOS_DIR=${PROJECT_SOURCE_DIR}/repos"
                "-DOPENCATTUS_REPOS_FILES=${opencattus_EMBEDDED_REPOS_FILES}"
                "-DOPENCATTUS_OUTPUT=${opencattus_EMBEDDED_REPOS_HEADER}"
                -P "${PROJECT_SOURCE_DIR}/cmake/GenerateEmbeddedRepos.cmake"
        DEPENDS
                ${_opencattus_embedded_repos_inputs}
                "${PROJECT_SOURCE_DIR}/cmake/GenerateEmbeddedRepos.cmake"
        COMMENT "Embedding repository configuration files"
        VERBATIM)

add_custom_target(opencattus_embedded_repos
        DEPENDS "${opencattus_EMBEDDED_REPOS_HEADER}")
