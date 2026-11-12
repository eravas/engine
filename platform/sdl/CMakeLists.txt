# Desktop backend. SDL3 is the project's only third-party dependency and is
# used by this directory alone: a system SDL3 if CMake can find one,
# otherwise a pinned release fetched from GitHub and linked statically.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.20
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS CONFIG)
FetchContent_MakeAvailable(SDL3)

add_library(platform_sdl STATIC platform_sdl.c)
engine_c11(platform_sdl)
target_link_libraries(platform_sdl PUBLIC engine_options PRIVATE SDL3::SDL3)

add_library(engine_platform INTERFACE)
target_link_libraries(engine_platform INTERFACE platform_sdl)

# engine_add_app(<name> <sources>...): an executable whose entry point is
# app_main(), linked against the kernel and this backend.
function(engine_add_app name)
  add_executable(${name} ${ARGN})
  engine_c11(${name})
  target_link_libraries(${name} PRIVATE kernel platform_sdl)
endfunction()
