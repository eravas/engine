# Deterministic backend: no window, no devices, no dependencies.
add_library(platform_headless STATIC platform_headless.c)
engine_c11(platform_headless)
target_link_libraries(platform_headless PUBLIC engine_options)

# main() lives in its own library so that tests can supply their own.
add_library(platform_headless_main STATIC headless_main.c)
engine_c11(platform_headless_main)
target_link_libraries(platform_headless_main PUBLIC platform_headless)

add_library(engine_platform INTERFACE)
target_link_libraries(engine_platform INTERFACE platform_headless)

# engine_add_app(<name> <sources>...): an executable whose entry point is
# app_main(), linked against the kernel and this backend.
function(engine_add_app name)
  add_executable(${name} ${ARGN})
  engine_c11(${name})
  target_link_libraries(${name} PRIVATE kernel platform_headless_main)
endfunction()
