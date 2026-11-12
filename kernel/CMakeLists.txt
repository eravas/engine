# The kernel library is whatever .c files exist in this directory. Today
# there are none (only contracts), so `kernel` is header-only; it becomes a
# static library as soon as the first implementation file is added. The glob
# is re-evaluated on every build, so adding arena.c needs no CMake edit.
file(GLOB KERNEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

if(KERNEL_SOURCES)
  add_library(kernel STATIC ${KERNEL_SOURCES})
  engine_c11(kernel)
  # The loop calls platform functions, so the backend must follow the kernel
  # on the link line.
  target_link_libraries(kernel PUBLIC engine_options engine_platform)
else()
  add_library(kernel INTERFACE)
  target_link_libraries(kernel INTERFACE engine_options engine_platform)
endif()
