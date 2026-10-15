# The game stacks (dev/background/game-stacks.md): Windows PC prototypes on Baker. One
# build holds Baker, the shared base (base/), and every stack's game.
#
#   cmake -S stacks -B build-stacks -G Ninja && cmake --build build-stacks
#   ctest --test-dir build-stacks
#
# Third-party code is fetched at pinned releases, never vendored (stacks/THIRD_PARTY.md).
cmake_minimum_required(VERSION 3.25)  # FetchContent's SYSTEM
project(baker_stacks LANGUAGES C CXX)

option(STACKS_BUILD_TESTS "Build the stacks' tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Baker, from this repository. Its own tests and examples are its CI's business.
set(BAKER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(BAKER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BAKER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BAKER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${BAKER_ROOT} baker)

include(cmake/dependencies.cmake)

# Our code: warnings as errors, and no exceptions, as in Baker (D54).
function(stacks_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHs-c-)
    target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
                               NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror
                           -fno-exceptions)
  endif()
endfunction()

if(STACKS_BUILD_TESTS)
  enable_testing()
  add_library(stacks_doctest_main STATIC ${BAKER_ROOT}/tests/doctest_impl.cpp
              ${BAKER_ROOT}/tests/doctest_main.cpp)
  target_include_directories(stacks_doctest_main SYSTEM PUBLIC ${BAKER_ROOT}/third_party/doctest)
  target_compile_definitions(stacks_doctest_main PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS
                             DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
  if(MSVC)
    target_compile_options(stacks_doctest_main PUBLIC /EHs-c-)
    target_compile_definitions(stacks_doctest_main PUBLIC _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(stacks_doctest_main PUBLIC -fno-exceptions)
  endif()
endif()

add_subdirectory(base)
add_subdirectory(cube)
add_subdirectory(duel)
add_subdirectory(ledger)
add_subdirectory(contraption)
add_subdirectory(stuntline)
add_subdirectory(hearth)
add_subdirectory(wayfarer)
