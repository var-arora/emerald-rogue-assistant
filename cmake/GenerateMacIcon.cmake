foreach(required_variable IN ITEMS
        ROGUE_ICON_SOURCE
        ROGUE_ICON_OUTPUT
        ROGUE_SIPS_EXECUTABLE)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

if(NOT EXISTS ${ROGUE_ICON_SOURCE})
  message(FATAL_ERROR "Icon source does not exist: ${ROGUE_ICON_SOURCE}")
endif()

execute_process(
  COMMAND
    ${ROGUE_SIPS_EXECUTABLE}
    --setProperty format icns
    ${ROGUE_ICON_SOURCE}
    --out ${ROGUE_ICON_OUTPUT}
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sips failed while generating the macOS icon: ${error}")
endif()

if(NOT EXISTS ${ROGUE_ICON_OUTPUT})
  message(FATAL_ERROR "sips did not create the macOS icon")
endif()
