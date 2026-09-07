set(
  ROGUE_PACKAGE_PLATFORM
  ""
  CACHE STRING
  "Package platform suffix; release presets set this value"
)

if(ROGUE_PACKAGE_PLATFORM STREQUAL "")
  if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(ROGUE_PACKAGE_PLATFORM windows-x64)
    else()
      set(ROGUE_PACKAGE_PLATFORM windows-unsupported)
    endif()
  elseif(APPLE)
    set(ROGUE_PACKAGE_PLATFORM macos-${CMAKE_SYSTEM_PROCESSOR})
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ROGUE_PACKAGE_PLATFORM linux-${CMAKE_SYSTEM_PROCESSOR})
  else()
    set(ROGUE_PACKAGE_PLATFORM ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
  endif()
endif()

if(ROGUE_PACKAGE_PLATFORM STREQUAL "windows-x64")
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "windows-x64 packages require a 64-bit Windows toolchain")
  endif()
elseif(ROGUE_PACKAGE_PLATFORM STREQUAL "macos-arm64")
  if(NOT APPLE)
    message(FATAL_ERROR "macos-arm64 packages require an arm64-only macOS toolchain")
  endif()
  if(CMAKE_OSX_ARCHITECTURES)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      message(FATAL_ERROR "macos-arm64 packages require an arm64-only macOS toolchain")
    endif()
  elseif(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR "macos-arm64 packages require an arm64-only macOS toolchain")
  endif()
elseif(ROGUE_PACKAGE_PLATFORM STREQUAL "linux-x86_64")
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
     NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "linux-x86_64 packages require a Linux x86_64 toolchain")
  endif()
endif()
