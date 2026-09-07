include(FetchContent)

if(DEFINED CMAKE_INSTALL_DEFAULT_COMPONENT_NAME)
    set(ROGUE_SAVED_DEFAULT_INSTALL_COMPONENT ${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME})
else()
    set(ROGUE_SAVED_DEFAULT_INSTALL_COMPONENT Unspecified)
endif()
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME RogueDependencies)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(SFML_USE_SYSTEM_DEPS OFF CACHE BOOL "" FORCE)
if(MSVC)
    set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
endif()

set(ROGUE_SFML_PATCH_ARGUMENTS)
if(APPLE)
    # SFML 3.1.0 has Retina coordinate conversion internally but disables the
    # high-resolution OpenGL surface that Emerald Rogue Assistant's scaled UI needs.
    list(
        APPEND ROGUE_SFML_PATCH_ARGUMENTS
        PATCH_COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=<SOURCE_DIR>"
            -P "${CMAKE_CURRENT_LIST_DIR}/patches/EnableSfmlMacosHighDpi.cmake"
    )
endif()

FetchContent_Declare(
    SFML
    URL https://github.com/SFML/SFML/archive/refs/tags/3.1.0.tar.gz
    URL_HASH SHA256=91209a112c2bd0bc6f4ce0d5f3e413cfb48b57c0de59f5507dc81f71b1ad7a5c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    ${ROGUE_SFML_PATCH_ARGUMENTS}
)
unset(ROGUE_SFML_PATCH_ARGUMENTS)

FetchContent_Declare(
    ENet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
    URL_HASH SHA256=28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SFML ENET)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME ${ROGUE_SAVED_DEFAULT_INSTALL_COMPONENT})
unset(ROGUE_SAVED_DEFAULT_INSTALL_COMPONENT)

# Do not treat warnings from SFML's headers as errors in our code.
set_target_properties(sfml-system sfml-window sfml-graphics PROPERTIES SYSTEM TRUE)

FetchContent_GetProperties(SFML SOURCE_DIR ROGUE_SFML_SOURCE_DIR)
FetchContent_GetProperties(ENet SOURCE_DIR ROGUE_ENET_SOURCE_DIR)
FetchContent_GetProperties(Freetype SOURCE_DIR ROGUE_FREETYPE_SOURCE_DIR)
FetchContent_GetProperties(HarfBuzz SOURCE_DIR ROGUE_HARFBUZZ_SOURCE_DIR)
FetchContent_GetProperties(SheenBidi SOURCE_DIR ROGUE_SHEENBIDI_SOURCE_DIR)

set(ROGUE_LICENSE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/licenses)
file(MAKE_DIRECTORY ${ROGUE_LICENSE_DIRECTORY})

function(rogue_copy_dependency_license source output_name)
    if(NOT EXISTS ${source})
        message(FATAL_ERROR "Dependency license file is missing: ${source}")
    endif()
    configure_file(${source} ${ROGUE_LICENSE_DIRECTORY}/${output_name} COPYONLY)
endfunction()

rogue_copy_dependency_license(${ROGUE_SFML_SOURCE_DIR}/license.md SFML.txt)
rogue_copy_dependency_license(${ROGUE_ENET_SOURCE_DIR}/LICENSE ENet.txt)
rogue_copy_dependency_license(${ROGUE_FREETYPE_SOURCE_DIR}/docs/FTL.TXT FreeType.txt)
rogue_copy_dependency_license(${ROGUE_HARFBUZZ_SOURCE_DIR}/COPYING HarfBuzz.txt)
rogue_copy_dependency_license(${ROGUE_SHEENBIDI_SOURCE_DIR}/LICENSE SheenBidi.txt)

set(
    ROGUE_DEPENDENCY_LICENSE_FILES
    ${ROGUE_LICENSE_DIRECTORY}/SFML.txt
    ${ROGUE_LICENSE_DIRECTORY}/ENet.txt
    ${ROGUE_LICENSE_DIRECTORY}/FreeType.txt
    ${ROGUE_LICENSE_DIRECTORY}/HarfBuzz.txt
    ${ROGUE_LICENSE_DIRECTORY}/SheenBidi.txt
)
