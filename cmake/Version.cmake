set(ROGUE_ASSISTANT_VERSION_CORE 1.0.0)
set(ROGUE_RELEASE_TAG "" CACHE STRING "Exact Git tag to build as a named release; empty for development builds")

if(NOT ROGUE_ASSISTANT_VERSION_CORE MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "ROGUE_ASSISTANT_VERSION_CORE must contain three numeric components")
endif()
find_package(Git QUIET)
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." rogue_version_root)
set(rogue_commit "")
set(rogue_dirty "")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --show-toplevel
        WORKING_DIRECTORY "${rogue_version_root}"
        OUTPUT_VARIABLE rogue_git_root OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if(NOT rogue_git_root STREQUAL "")
        file(REAL_PATH "${rogue_git_root}" rogue_git_root)
    endif()
    if(rogue_git_root STREQUAL rogue_version_root)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
            WORKING_DIRECTORY "${rogue_version_root}"
            OUTPUT_VARIABLE rogue_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${rogue_version_root}"
            OUTPUT_VARIABLE rogue_dirty OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE rogue_status_result ERROR_QUIET
        )
        if(NOT rogue_status_result EQUAL 0)
            message(FATAL_ERROR "Cannot check the source tree for uncommitted changes")
        endif()
    endif()
endif()

if(NOT ROGUE_RELEASE_TAG STREQUAL "")
    if(NOT ROGUE_RELEASE_TAG MATCHES "^v([0-9]+[.][0-9]+[.][0-9]+)(-([0-9A-Za-z-]+([.][0-9A-Za-z-]+)*))?$")
        message(FATAL_ERROR "Release tags must use vMAJOR.MINOR.PATCH with an optional prerelease name")
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL ROGUE_ASSISTANT_VERSION_CORE)
        message(FATAL_ERROR "The release tag must match ROGUE_ASSISTANT_VERSION_CORE")
    endif()
    set(ROGUE_ASSISTANT_VERSION_PRERELEASE "${CMAKE_MATCH_3}")
    string(REPLACE "." ";" rogue_prerelease_parts "${ROGUE_ASSISTANT_VERSION_PRERELEASE}")
    foreach(part IN LISTS rogue_prerelease_parts)
        if(part MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "Numeric prerelease parts must not have leading zeros")
        endif()
    endforeach()
    if(rogue_commit STREQUAL "" OR NOT rogue_dirty STREQUAL "")
        message(FATAL_ERROR "Named releases require a clean Git checkout")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify "refs/tags/${ROGUE_RELEASE_TAG}^{commit}"
        WORKING_DIRECTORY "${rogue_version_root}"
        OUTPUT_VARIABLE rogue_tag_commit OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rogue_tag_result ERROR_QUIET
    )
    if(NOT rogue_tag_result EQUAL 0 OR NOT rogue_tag_commit STREQUAL rogue_commit)
        message(FATAL_ERROR "The release tag must exist and point to the checked-out commit")
    endif()
    string(SUBSTRING "${ROGUE_RELEASE_TAG}" 1 -1 ROGUE_ASSISTANT_VERSION)
    set(ROGUE_ASSISTANT_BUILD_LABEL "${ROGUE_ASSISTANT_VERSION}")
else()
    if(rogue_commit STREQUAL "")
        set(rogue_short_commit unknown)
    else()
        string(SUBSTRING "${rogue_commit}" 0 12 rogue_short_commit)
    endif()
    set(ROGUE_ASSISTANT_VERSION_PRERELEASE "dev.g${rogue_short_commit}")
    set(ROGUE_ASSISTANT_BUILD_LABEL "dev-${rogue_short_commit}")
    if(NOT rogue_dirty STREQUAL "")
        string(APPEND ROGUE_ASSISTANT_VERSION_PRERELEASE ".dirty")
        string(APPEND ROGUE_ASSISTANT_BUILD_LABEL "-dirty")
    endif()
    set(ROGUE_ASSISTANT_VERSION "${ROGUE_ASSISTANT_VERSION_CORE}-${ROGUE_ASSISTANT_VERSION_PRERELEASE}")
endif()
