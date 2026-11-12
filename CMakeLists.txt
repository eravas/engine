cmake_minimum_required(VERSION 3.25)

# Which backend implements kernel/platform.h. Normally set by a preset.
set(ENGINE_PLATFORM "headless" CACHE STRING
    "Platform backend: headless, sdl or rp2350")
set_property(CACHE ENGINE_PLATFORM PROPERTY STRINGS headless sdl rp2350)

if(ENGINE_PLATFORM STREQUAL "rp2350")
  # The Pico SDK selects the cross toolchain, so it must load before project().
  include(cmake/pico_sdk.cmake)
endif()

project(engine LANGUAGES C)

if(ENGINE_PLATFORM STREQUAL "rp2350")
  # Required by the SDK's own sources. No engine code is C++ or assembly.
  enable_language(CXX ASM)
  pico_sdk_init()
endif()

# Flags and include root shared by all of our own code. Sources include
# headers by repository-relative path: "kernel/platform.h".
add_library(engine_options INTERFACE)
target_include_directories(engine_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(engine_options INTERFACE -Wall -Wextra -Werror)

# Strict ISO C11 for a target. Kept off third-party code (SDL, Pico SDK).
function(engine_c11 target)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endfunction()

add_subdirectory(platform/${ENGINE_PLATFORM})
add_subdirectory(kernel)

include(CTest)
add_subdirectory(tests)
