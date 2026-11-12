# The smoke app builds for every backend: it is the proof that portable code
# links against each of them.
engine_add_app(smoke smoke/smoke_app.c)

# The test runner needs a hosted C library, so it is not built for the MCU.
if(NOT ENGINE_PLATFORM STREQUAL "rp2350")
  add_library(test_runner STATIC support/test.c)
  engine_c11(test_runner)
  target_link_libraries(test_runner PUBLIC engine_options)
endif()

if(ENGINE_PLATFORM STREQUAL "sdl")
  # Everything here runs without a display or sound card, on SDL's dummy
  # drivers.
  set(SDL_DUMMY "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

  add_test(NAME smoke_sdl
    COMMAND smoke --frames 10 --storage-dir ${CMAKE_CURRENT_BINARY_DIR}/storage)
  set_tests_properties(smoke_sdl PROPERTIES
    LABELS smoke TIMEOUT 60 ENVIRONMENT "${SDL_DUMMY}")

  # Storage conformance. See sdl/test_sdl_storage.c for the three scenarios.
  set(SDL_STORAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/sdl_storage)
  engine_add_app(test_sdl_storage sdl/test_sdl_storage.c)
  target_compile_definitions(test_sdl_storage PRIVATE
    TEST_STORAGE_DIR="${SDL_STORAGE_DIR}")
  target_link_libraries(test_sdl_storage PRIVATE test_runner)

  add_test(NAME sdl_storage_clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SDL_STORAGE_DIR})
  add_test(NAME sdl_storage_prepare
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SDL_STORAGE_DIR}/slot5.bin)
  set_tests_properties(sdl_storage_prepare PROPERTIES DEPENDS sdl_storage_clean)
  set_tests_properties(sdl_storage_clean sdl_storage_prepare PROPERTIES
    FIXTURES_SETUP sdl_storage_dir LABELS unit)

  add_test(NAME sdl_storage
    COMMAND test_sdl_storage --storage-dir ${SDL_STORAGE_DIR})
  set_tests_properties(sdl_storage PROPERTIES
    FIXTURES_REQUIRED sdl_storage_dir ENVIRONMENT "${SDL_DUMMY}")

  # A directory that cannot exist: its parent is a regular file.
  add_test(NAME sdl_storage_bad_dir
    COMMAND test_sdl_storage
      --storage-dir ${CMAKE_CURRENT_LIST_FILE}/storage)
  # A directory name too long for the backend's path buffer.
  string(REPEAT "long_directory_name/" 60 SDL_LONG_DIR)
  add_test(NAME sdl_storage_long_dir
    COMMAND test_sdl_storage
      --storage-dir ${CMAKE_CURRENT_BINARY_DIR}/${SDL_LONG_DIR})
  set_tests_properties(sdl_storage_bad_dir sdl_storage_long_dir PROPERTIES
    ENVIRONMENT "${SDL_DUMMY};STORAGE_MODE=broken")

  set_tests_properties(sdl_storage sdl_storage_bad_dir sdl_storage_long_dir
    PROPERTIES LABELS unit TIMEOUT 60)
endif()

# Everything below needs the headless backend's control surface.
if(NOT ENGINE_PLATFORM STREQUAL "headless")
  return()
endif()

set(TEST_TMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/tmp)
file(MAKE_DIRECTORY ${TEST_TMP_DIR}/smoke)

add_library(test_support STATIC support/determinism.c)
engine_c11(test_support)
target_link_libraries(test_support PUBLIC test_runner platform_headless)

# engine_add_test(<name> <label> <source> [EXCLUDE_FROM_ALL])
function(engine_add_test name label source)
  add_executable(${name} ${ARGN} ${source})
  engine_c11(${name})
  target_compile_definitions(${name} PRIVATE TEST_TMP_DIR="${TEST_TMP_DIR}")
  target_link_libraries(${name} PRIVATE test_support kernel)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()

# ---- unit: must pass -------------------------------------------------------
engine_add_test(test_headless unit test_headless.c)

add_test(NAME smoke_headless
  COMMAND smoke --frames 5 --dump-dir ${TEST_TMP_DIR}/smoke)
add_test(NAME smoke_headless_script
  COMMAND smoke --input ${CMAKE_CURRENT_SOURCE_DIR}/smoke/input.txt)
set_tests_properties(smoke_headless smoke_headless_script PROPERTIES
  LABELS smoke)

# ---- determinism: must pass ------------------------------------------------
engine_add_test(test_determinism determinism test_determinism.c)

# ---- lint: must pass -------------------------------------------------------
add_executable(srclint lint/srclint.c)
engine_c11(srclint)
target_link_libraries(srclint PRIVATE engine_options)

file(GLOB_RECURSE ENGINE_CODE CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/kernel/*.[ch]
  ${CMAKE_SOURCE_DIR}/sim/*.[ch]
  ${CMAKE_SOURCE_DIR}/game/*.[ch])
add_test(NAME lint_engine_code
  COMMAND srclint ${ENGINE_CODE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke/smoke_app.c)

# The linter's own tests: a clean fixture, and one with a known finding count.
add_test(NAME lint_accepts_clean_fixture
  COMMAND srclint ${CMAKE_CURRENT_SOURCE_DIR}/lint/fixtures/clean.c)
add_test(NAME lint_rejects_dirty_fixture
  COMMAND srclint ${CMAKE_CURRENT_SOURCE_DIR}/lint/fixtures/dirty.c)
set_tests_properties(lint_rejects_dirty_fixture PROPERTIES
  PASS_REGULAR_EXPRESSION "srclint: 13 finding")
set_tests_properties(lint_engine_code lint_accepts_clean_fixture
  lint_rejects_dirty_fixture PROPERTIES LABELS lint)

# ---- todo: reported, never fatal -------------------------------------------
# Specifications of code that is not written yet. The executables are left
# out of the default build because they cannot link until kernel/<name>.c
# exists. Each is built by a CTest fixture instead, so `ctest -L todo` shows
# "build failed / not run" per component and never breaks the main build.
foreach(name arena pool loop)
  engine_add_test(todo_${name} todo todo/test_${name}.c EXCLUDE_FROM_ALL)
  add_test(NAME todo_${name}_build
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target todo_${name})
  set_tests_properties(todo_${name}_build PROPERTIES
    LABELS todo
    FIXTURES_SETUP todo_${name}_built
    RESOURCE_LOCK todo_build)
  set_tests_properties(todo_${name} PROPERTIES
    FIXTURES_REQUIRED todo_${name}_built)
endforeach()
