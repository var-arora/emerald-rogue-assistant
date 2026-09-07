cmake_minimum_required(VERSION 3.25)

include(cmake/Version.cmake)

project(
    RogueAssistant
    VERSION ${ROGUE_ASSISTANT_VERSION_CORE}
    DESCRIPTION "Cross-platform companion application for Pokémon Emerald Rogue"
    LANGUAGES C CXX
)

set(ROGUE_ASSISTANT_APPLICATION_ID "assistant.emerald.rogue")
set(ROGUE_ASSISTANT_DISPLAY_NAME "Emerald Rogue Assistant")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/RogueAssistant-build.txt"
    CONTENT "${ROGUE_ASSISTANT_VERSION}\n${ROGUE_ASSISTANT_BUILD_LABEL}\n")

if(MSVC)
    # Use the static runtime so the ZIP works without a separate Visual C++
    # Redistributable install.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(ROGUE_BUILD_TESTS "Build Emerald Rogue Assistant tests" ${BUILD_TESTING})
option(ROGUE_BUILD_DESKTOP "Build the standalone Emerald Rogue Assistant desktop application" ON)
option(ROGUE_WARNINGS_AS_ERRORS "Treat Emerald Rogue Assistant warnings as errors" OFF)
option(ROGUE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ROGUE_ENABLE_CLANG_TIDY "Run clang-tidy on Emerald Rogue Assistant targets" OFF)

add_library(rogue_project_options INTERFACE)
target_compile_features(rogue_project_options INTERFACE cxx_std_20)
if(WIN32)
    target_compile_definitions(rogue_project_options INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_library(rogue_project_warnings INTERFACE)
if(MSVC)
    target_compile_options(rogue_project_warnings INTERFACE /W4 /permissive-)
    if(ROGUE_WARNINGS_AS_ERRORS)
        target_compile_options(rogue_project_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rogue_project_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(ROGUE_WARNINGS_AS_ERRORS)
        target_compile_options(rogue_project_warnings INTERFACE -Werror)
    endif()
endif()

add_library(rogue_sanitizers INTERFACE)
if(ROGUE_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            rogue_sanitizers
            INTERFACE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
        )
        target_link_options(rogue_sanitizers INTERFACE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "ROGUE_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

if(ROGUE_ENABLE_CLANG_TIDY)
    find_program(ROGUE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

function(rogue_enable_clang_tidy target)
    if(ROGUE_ENABLE_CLANG_TIDY)
        set_property(
            TARGET ${target}
            PROPERTY CXX_CLANG_TIDY
                "${ROGUE_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
        )
    endif()
endfunction()

configure_file(
    cmake/RogueAssistantVersion.h.in
    generated/RogueAssistantVersionGenerated.h
    @ONLY
)

add_library(
    rogue_core
    STATIC
        RogueAssistantCpp/Src/DataStream.cpp
        RogueAssistantCpp/Src/GameData.cpp
        RogueAssistantCpp/Src/Log.cpp
        RogueAssistantCpp/Src/Platform/AppPaths.cpp
        RogueAssistantCpp/Src/Platform/BridgeScript.cpp
        RogueAssistantCpp/Src/Platform/Configuration.cpp
        RogueAssistantCpp/Src/Platform/FileSystem.cpp
        RogueAssistantCpp/Src/Platform/ResourceLocator.cpp
        RogueAssistantCpp/Src/Platform/Utf8.cpp
        RogueAssistantCpp/Src/Storage/HomeBoxFile.cpp
        RogueAssistantCpp/Src/Timer.cpp
        RogueAssistantCpp/Src/UserData.cpp
)
target_include_directories(
    rogue_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/RogueAssistantCpp/Inc
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(
    rogue_core
    PUBLIC
        rogue_project_options
        rogue_sanitizers
    PRIVATE
        rogue_project_warnings
)
rogue_enable_clang_tidy(rogue_core)
if(WIN32)
    target_link_libraries(rogue_core PRIVATE shell32)
endif()

add_library(
    rogue_bridge
    STATIC
        RogueAssistantCpp/Src/Bridge/BridgeProtocol.cpp
        RogueAssistantCpp/Src/Bridge/GameMemoryTransport.cpp
        RogueAssistantCpp/Src/Bridge/GameSession.cpp
        RogueAssistantCpp/Src/Bridge/TcpLuaTransport.cpp
        RogueAssistantCpp/Src/Bridge/TcpSocket.cpp
)
target_include_directories(rogue_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/RogueAssistantCpp/Inc)
target_link_libraries(
    rogue_bridge
    PUBLIC
        rogue_core
        rogue_project_options
        rogue_sanitizers
    PRIVATE
        rogue_project_warnings
)
rogue_enable_clang_tidy(rogue_bridge)
if(WIN32)
    target_link_libraries(rogue_bridge PRIVATE ws2_32)
endif()

include(cmake/DesktopDependencies.cmake)

add_library(
    rogue_multiplayer
    OBJECT
        RogueAssistantCpp/Src/Behaviours/MultiplayerBehaviour.cpp
        RogueAssistantCpp/Src/Multiplayer/MultiplayerProtocol.cpp
)
target_include_directories(
    rogue_multiplayer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/RogueAssistantCpp/Inc
        ${enet_SOURCE_DIR}/include
)
target_link_libraries(
    rogue_multiplayer
    PUBLIC
        enet
        rogue_bridge
        rogue_project_options
        rogue_sanitizers
    PRIVATE
        rogue_project_warnings
)
if(WIN32)
    target_link_libraries(rogue_multiplayer PUBLIC winmm ws2_32)
endif()
rogue_enable_clang_tidy(rogue_multiplayer)

add_library(
    rogue_app
    STATIC
        RogueAssistantCpp/Src/Application/Application.cpp
        RogueAssistantCpp/Src/Application/CommandLine.cpp
        RogueAssistantCpp/Src/Application/SessionWorker.cpp
        RogueAssistantCpp/Src/Behaviours/CommonBehaviour.cpp
        RogueAssistantCpp/Src/Behaviours/HomeBoxBehaviour.cpp
        RogueAssistantCpp/Src/GameConnection.cpp
        RogueAssistantCpp/Src/GameConnectionManager.cpp
        RogueAssistantCpp/Src/ObservedGameMemory.cpp
)
target_include_directories(rogue_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/RogueAssistantCpp/Inc)
target_link_libraries(
    rogue_app
    PUBLIC
        rogue_bridge
        rogue_multiplayer
        rogue_project_options
        rogue_sanitizers
        Threads::Threads
    PRIVATE
        rogue_project_warnings
)
rogue_enable_clang_tidy(rogue_app)

set(
    ROGUE_RESOURCE_FILES
    RogueAssistantCpp/Assets/WobbuffetImage.png
    RogueAssistantCpp/Assets/WobbuffetIcon.ico
    RogueAssistantCpp/Assets/poketch_frame.png
    RogueAssistantCpp/Assets/pokemon-emerald-pro.ttf
    RogueAssistantCpp/Assets/RogueAssistant_mGBA.lua
)

set(
    ROGUE_ROOT_DOCUMENT_FILES
    README.md
    THIRD_PARTY_NOTICES.md
)

set(
    ROGUE_GUIDE_DOCUMENT_FILES
    docs/architecture.md
    docs/bridge-protocol.md
    docs/development.md
    docs/home-box-format.md
    docs/installation.md
    docs/multiplayer-protocol.md
    docs/release.md
    docs/troubleshooting.md
)

set(ROGUE_DOCUMENT_FILES ${ROGUE_ROOT_DOCUMENT_FILES} ${ROGUE_GUIDE_DOCUMENT_FILES})

if(ROGUE_BUILD_DESKTOP)
    set(ROGUE_DESKTOP_SOURCES)
    if(WIN32)
        enable_language(RC)
        configure_file(
            cmake/WindowsVersion.rc.in
            generated/RogueAssistantVersion.rc
            @ONLY
        )
        list(APPEND ROGUE_DESKTOP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/generated/RogueAssistantVersion.rc)
    endif()

    add_executable(
        RogueAssistant
        WIN32
        MACOSX_BUNDLE
            RogueAssistantCpp/Src/DesktopMain.cpp
            RogueAssistantCpp/Src/UI/PrimaryUI.cpp
            RogueAssistantCpp/Src/UI/Window.cpp
            ${ROGUE_DESKTOP_SOURCES}
    )
    target_link_libraries(
        RogueAssistant
        PRIVATE
            rogue_app
            rogue_project_warnings
            sfml-graphics
            sfml-window
            sfml-system
    )
    if(WIN32)
        target_link_libraries(RogueAssistant PRIVATE sfml-main)
    endif()
    rogue_enable_clang_tidy(RogueAssistant)
    set_target_properties(
        RogueAssistant
        PROPERTIES
            OUTPUT_NAME RogueAssistant
            MACOSX_BUNDLE_BUNDLE_NAME "${ROGUE_ASSISTANT_DISPLAY_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${ROGUE_ASSISTANT_APPLICATION_ID}"
            MACOSX_BUNDLE_INFO_STRING "${ROGUE_ASSISTANT_DISPLAY_NAME} ${ROGUE_ASSISTANT_VERSION}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${ROGUE_ASSISTANT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )

    if(APPLE)
        find_program(ROGUE_SIPS_EXECUTABLE sips REQUIRED)
        set(ROGUE_MACOS_ICON ${CMAKE_CURRENT_BINARY_DIR}/generated/RogueAssistant.icns)
        add_custom_command(
            OUTPUT ${ROGUE_MACOS_ICON}
            COMMAND
                ${CMAKE_COMMAND}
                -DROGUE_ICON_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/RogueAssistantCpp/Assets/WobbuffetImage.png
                -DROGUE_ICON_OUTPUT=${ROGUE_MACOS_ICON}
                -DROGUE_SIPS_EXECUTABLE=${ROGUE_SIPS_EXECUTABLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateMacIcon.cmake
            DEPENDS
                RogueAssistantCpp/Assets/WobbuffetImage.png
                cmake/GenerateMacIcon.cmake
            VERBATIM
        )
        set_source_files_properties(
            ${ROGUE_RESOURCE_FILES}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources
        )
        set_source_files_properties(
            ${ROGUE_ROOT_DOCUMENT_FILES}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Documentation
        )
        set_source_files_properties(
            ${ROGUE_GUIDE_DOCUMENT_FILES}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Documentation/docs
        )
        set_source_files_properties(
            ${ROGUE_DEPENDENCY_LICENSE_FILES}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Documentation/licenses
        )
        set_source_files_properties(
            ${ROGUE_MACOS_ICON}
            PROPERTIES
                GENERATED TRUE
                MACOSX_PACKAGE_LOCATION Resources
        )
        target_sources(
            RogueAssistant
            PRIVATE
                ${ROGUE_RESOURCE_FILES}
                ${ROGUE_DOCUMENT_FILES}
                ${ROGUE_DEPENDENCY_LICENSE_FILES}
                ${ROGUE_MACOS_ICON}
        )
        set_target_properties(
            RogueAssistant
            PROPERTIES MACOSX_BUNDLE_ICON_FILE RogueAssistant.icns
        )
        install(TARGETS RogueAssistant BUNDLE DESTINATION . COMPONENT RogueAssistant)
    else()
        add_custom_command(
            TARGET RogueAssistant
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RogueAssistant>/resources"
        )
        foreach(resource IN LISTS ROGUE_RESOURCE_FILES)
            get_filename_component(resource_name "${resource}" NAME)
            add_custom_command(
                TARGET RogueAssistant
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${resource}"
                    "$<TARGET_FILE_DIR:RogueAssistant>/resources/${resource_name}"
            )
        endforeach()
        install(
            TARGETS RogueAssistant
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT RogueAssistant
        )
        install(
            FILES ${ROGUE_RESOURCE_FILES}
            DESTINATION ${CMAKE_INSTALL_BINDIR}/resources
            COMPONENT RogueAssistant
        )
    endif()

    if(NOT APPLE)
        install(
            FILES ${ROGUE_ROOT_DOCUMENT_FILES}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/RogueAssistant
            COMPONENT RogueAssistant
        )
        install(
            FILES ${ROGUE_GUIDE_DOCUMENT_FILES}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/RogueAssistant/docs
            COMPONENT RogueAssistant
        )
        install(
            FILES ${ROGUE_DEPENDENCY_LICENSE_FILES}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/RogueAssistant/licenses
            COMPONENT RogueAssistant
        )
    endif()

    if(UNIX AND NOT APPLE)
        install(
            FILES packaging/linux/${ROGUE_ASSISTANT_APPLICATION_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            COMPONENT RogueAssistant
        )
        install(
            FILES RogueAssistantCpp/Assets/WobbuffetImage.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
            RENAME ${ROGUE_ASSISTANT_APPLICATION_ID}.png
            COMPONENT RogueAssistant
        )
    endif()
endif()

if(ROGUE_BUILD_TESTS)
    include(cmake/Dependencies.cmake)
    add_subdirectory(tests)
endif()

if(ROGUE_BUILD_DESKTOP)
    include(cmake/Packaging.cmake)
endif()
