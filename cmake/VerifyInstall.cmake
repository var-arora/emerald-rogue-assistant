foreach(required_variable IN ITEMS
        ROGUE_INSTALL_ROOT
        ROGUE_INSTALL_PLATFORM
        ROGUE_EXPECTED_VERSION)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

if(ROGUE_INSTALL_PLATFORM STREQUAL "windows")
  set(executable ${ROGUE_INSTALL_ROOT}/bin/RogueAssistant.exe)
  set(resource_directory ${ROGUE_INSTALL_ROOT}/bin/resources)
elseif(ROGUE_INSTALL_PLATFORM STREQUAL "macos")
  set(executable ${ROGUE_INSTALL_ROOT}/RogueAssistant.app/Contents/MacOS/RogueAssistant)
  set(resource_directory ${ROGUE_INSTALL_ROOT}/RogueAssistant.app/Contents/Resources)
  set(document_directory ${resource_directory}/Documentation)
  set(bundle_plist ${ROGUE_INSTALL_ROOT}/RogueAssistant.app/Contents/Info.plist)
elseif(ROGUE_INSTALL_PLATFORM STREQUAL "linux")
  set(executable ${ROGUE_INSTALL_ROOT}/bin/RogueAssistant)
  set(resource_directory ${ROGUE_INSTALL_ROOT}/bin/resources)
else()
  message(FATAL_ERROR "Unsupported install platform: ${ROGUE_INSTALL_PLATFORM}")
endif()

if(NOT DEFINED document_directory)
  set(document_directory ${ROGUE_INSTALL_ROOT}/share/doc/RogueAssistant)
endif()

function(verify_directory_inventory directory)
  set(expected_entries ${ARGN})
  file(GLOB actual_entries LIST_DIRECTORIES true RELATIVE "${directory}" "${directory}/*")
  list(SORT expected_entries)
  list(SORT actual_entries)
  if(NOT "${actual_entries}" STREQUAL "${expected_entries}")
    message(
      FATAL_ERROR
      "Unexpected package inventory in ${directory}\n"
      "Expected: ${expected_entries}\n"
      "Actual:   ${actual_entries}"
    )
  endif()
endfunction()

set(
  required_files
  ${executable}
  ${resource_directory}/WobbuffetImage.png
  ${resource_directory}/WobbuffetIcon.ico
  ${resource_directory}/poketch_frame.png
  ${resource_directory}/pokemon-emerald-pro.ttf
  ${resource_directory}/RogueAssistant_mGBA.lua
  ${document_directory}/README.md
  ${document_directory}/THIRD_PARTY_NOTICES.md
  ${document_directory}/docs/architecture.md
  ${document_directory}/docs/bridge-protocol.md
  ${document_directory}/docs/development.md
  ${document_directory}/docs/home-box-format.md
  ${document_directory}/docs/installation.md
  ${document_directory}/docs/multiplayer-protocol.md
  ${document_directory}/docs/release.md
  ${document_directory}/docs/troubleshooting.md
  ${document_directory}/licenses/SFML.txt
  ${document_directory}/licenses/ENet.txt
  ${document_directory}/licenses/FreeType.txt
  ${document_directory}/licenses/HarfBuzz.txt
  ${document_directory}/licenses/SheenBidi.txt
)
if(ROGUE_INSTALL_PLATFORM STREQUAL "linux")
  list(
    APPEND
    required_files
    ${ROGUE_INSTALL_ROOT}/share/applications/assistant.emerald.rogue.desktop
    ${ROGUE_INSTALL_ROOT}/share/icons/hicolor/128x128/apps/assistant.emerald.rogue.png
  )
endif()
foreach(required_file IN LISTS required_files)
  if(NOT EXISTS ${required_file})
    message(FATAL_ERROR "Installed package is missing: ${required_file}")
  endif()
endforeach()

verify_directory_inventory(
  "${document_directory}"
  README.md
  THIRD_PARTY_NOTICES.md
  docs
  licenses
)
verify_directory_inventory(
  "${document_directory}/docs"
  architecture.md
  bridge-protocol.md
  development.md
  home-box-format.md
  installation.md
  multiplayer-protocol.md
  release.md
  troubleshooting.md
)
verify_directory_inventory(
  "${document_directory}/licenses"
  ENet.txt
  FreeType.txt
  HarfBuzz.txt
  SFML.txt
  SheenBidi.txt
)
if(ROGUE_INSTALL_PLATFORM STREQUAL "macos")
  verify_directory_inventory(
    "${resource_directory}"
    Documentation
    RogueAssistant.icns
    RogueAssistant_mGBA.lua
    WobbuffetIcon.ico
    WobbuffetImage.png
    pokemon-emerald-pro.ttf
    poketch_frame.png
  )
else()
  verify_directory_inventory(
    "${resource_directory}"
    RogueAssistant_mGBA.lua
    WobbuffetIcon.ico
    WobbuffetImage.png
    pokemon-emerald-pro.ttf
    poketch_frame.png
  )
endif()

execute_process(
  COMMAND ${executable} --version
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 10
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installed executable failed --version (${result}): ${error}")
endif()
string(STRIP "${output}" output)
set(expected_output "Emerald Rogue Assistant ${ROGUE_EXPECTED_VERSION}")
if(NOT "${output}" STREQUAL "${expected_output}")
  string(HEX "${output}" output_hex)
  string(HEX "${expected_output}" expected_output_hex)
  message(
    FATAL_ERROR
    "Unexpected installed version output\n"
    "Expected: ${expected_output}\n"
    "Actual:   ${output}\n"
    "Expected bytes: ${expected_output_hex}\n"
    "Actual bytes:   ${output_hex}"
  )
endif()

if(ROGUE_INSTALL_PLATFORM STREQUAL "macos")
  find_program(ROGUE_PLUTIL_EXECUTABLE NAMES plutil REQUIRED)
  string(REGEX MATCH "^[0-9]+[.][0-9]+[.][0-9]+" expected_version_core "${ROGUE_EXPECTED_VERSION}")
  foreach(key_and_expected IN ITEMS
          "CFBundleIdentifier|assistant.emerald.rogue"
          "CFBundleName|Emerald Rogue Assistant"
          "CFBundleShortVersionString|${expected_version_core}"
          "CFBundleLongVersionString|${ROGUE_EXPECTED_VERSION}")
    string(REPLACE "|" ";" key_and_expected "${key_and_expected}")
    list(GET key_and_expected 0 key)
    list(GET key_and_expected 1 expected)
    execute_process(
      COMMAND ${ROGUE_PLUTIL_EXECUTABLE} -extract ${key} raw -o - ${bundle_plist}
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE error
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Cannot read ${key} from the installed application (${result}): ${error}")
    endif()
    if(NOT "${output}" STREQUAL "${expected}")
      message(FATAL_ERROR "Unexpected ${key}: ${output}")
    endif()
  endforeach()
endif()
