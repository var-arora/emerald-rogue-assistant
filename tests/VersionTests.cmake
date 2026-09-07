cmake_minimum_required(VERSION 3.25)
find_package(Git REQUIRED)
include("${ROGUE_SOURCE_DIR}/cmake/Version.cmake")
set(core "${ROGUE_ASSISTANT_VERSION_CORE}")

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef test_id)
set(test_root "${ROGUE_TEST_PARENT}/rogue-version-${test_id}")
set(source "${test_root}/source")
file(MAKE_DIRECTORY "${source}/cmake")
file(COPY "${ROGUE_SOURCE_DIR}/cmake/Version.cmake"
    "${ROGUE_SOURCE_DIR}/cmake/WriteVersion.cmake" DESTINATION "${source}/cmake")

function(run_git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${source}" RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Git failed: ${ARGN}\n${output}\n${error}")
    endif()
endfunction()

function(check_version tag expected_version expected_label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROGUE_RELEASE_TAG=${tag}"
            "-DROGUE_VERSION_OUTPUT=${test_root}/version.txt"
            "-DROGUE_BUILD_LABEL_OUTPUT=${test_root}/label.txt"
            -P "${source}/cmake/WriteVersion.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Version check failed for '${tag}': ${output}\n${error}")
    endif()
    file(READ "${test_root}/version.txt" version)
    file(READ "${test_root}/label.txt" label)
    if(NOT version STREQUAL "${expected_version}\n" OR NOT label STREQUAL "${expected_label}\n")
        message(FATAL_ERROR "Wrong version or label for '${tag}': ${version} / ${label}")
    endif()
endfunction()

function(reject_release tag expected_error)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROGUE_RELEASE_TAG=${tag}"
            "-DROGUE_VERSION_OUTPUT=${test_root}/version.txt"
            -P "${source}/cmake/WriteVersion.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "${expected_error}")
        message(FATAL_ERROR "Expected rejection of '${tag}': ${output}\n${error}")
    endif()
endfunction()

# A source archive has no commit to report and must not claim to be a release.
check_version("" "${core}-dev.gunknown" "dev-unknown")
reject_release("v${core}" "clean Git checkout")

run_git(init --initial-branch=main)
run_git(config core.autocrlf false)
run_git(config user.name "Version test")
run_git(config user.email "version-test@example.invalid")
run_git(config commit.gpgsign false)
run_git(config tag.gpgsign false)
run_git(config core.hooksPath "${test_root}/no-hooks")
run_git(add cmake)
run_git(commit --no-verify -m "Test version labels")
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${source}" OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE)
set(dev_version "${core}-dev.g${commit}")
set(dev_label "dev-${commit}")
check_version("" "${dev_version}" "${dev_label}")

foreach(version IN ITEMS "${core}-alpha.0" "${core}-beta.1" "${core}-rc.1" "${core}")
    run_git(tag -a "v${version}" -m "Test tag")
    check_version("v${version}" "${version}" "${version}")
endforeach()
# A branch build remains a development build even when HEAD has release tags.
check_version("" "${dev_version}" "${dev_label}")
reject_release("${core}" "Release tags must")
reject_release("v${core}-alpha.01" "leading zeros")
reject_release("v9999.0.0" "must match ROGUE_ASSISTANT_VERSION_CORE")
reject_release("v${core}-alpha.9" "must exist and point")

file(WRITE "${source}/untracked.txt" "Uncommitted file\n")
check_version("" "${dev_version}.dirty" "${dev_label}-dirty")
reject_release("v${core}" "clean Git checkout")
run_git(add untracked.txt)
run_git(commit --no-verify -m "Move beyond release tag")
reject_release("v${core}" "must exist and point")
file(APPEND "${source}/untracked.txt" "Changed tracked file\n")
reject_release("v${core}" "clean Git checkout")

file(REMOVE_RECURSE "${test_root}")
message(STATUS "Build version tests passed")
