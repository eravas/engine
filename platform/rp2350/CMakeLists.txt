# RP2350 backend: a compile-only stub (see platform_rp2350.c).
#
# Pico SDK libraries are INTERFACE libraries whose sources are compiled into
# whatever target links them. To keep our flags off SDK code, the portable
# sources of an app go into a strict static library, and the executable
# itself holds only the backend file plus the SDK.
set(ENGINE_RP2350_BACKEND ${CMAKE_CURRENT_SOURCE_DIR}/platform_rp2350.c
    CACHE INTERNAL "")
set_source_files_properties(${ENGINE_RP2350_BACKEND} PROPERTIES
  COMPILE_OPTIONS "-Wall;-Wextra;-Werror")

# The backend is compiled into each executable, so there is no library here.
add_library(engine_platform INTERFACE)

# engine_add_app(<name> <sources>...): an ELF whose entry point is
# app_main(), linked against the kernel and this backend.
function(engine_add_app name)
  add_library(${name}_app STATIC ${ARGN})
  engine_c11(${name}_app)
  target_link_libraries(${name}_app PUBLIC kernel)

  add_executable(${name} ${ENGINE_RP2350_BACKEND})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE ${name}_app pico_stdlib)
endfunction()
