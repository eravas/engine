# Locates the Raspberry Pi Pico SDK and loads its pre-project() setup.
# Uses PICO_SDK_PATH (cache or environment) if set, otherwise fetches a
# pinned release. Only the SDK itself is needed: no submodules.

if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()

if(NOT PICO_SDK_PATH)
  include(FetchContent)
  FetchContent_Declare(pico_sdk
    GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
    GIT_TAG 2.3.1
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "")
  FetchContent_GetProperties(pico_sdk)
  if(NOT pico_sdk_POPULATED)
    message(STATUS "Fetching Pico SDK (set PICO_SDK_PATH to use a local copy)")
    FetchContent_Populate(pico_sdk)
  endif()
  set(PICO_SDK_PATH "${pico_sdk_SOURCE_DIR}")
endif()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Pico SDK")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico SDK platform")
set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
