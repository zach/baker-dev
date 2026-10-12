cmake_minimum_required(VERSION 3.20)
project(moira VERSION 0.0.1 LANGUAGES CXX)

option(MOIRA_BUILD_SCRIPT     "Build moira::script (evaluation, objects, tasks)" ON)
option(MOIRA_BUILD_TESTS      "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(MOIRA_ATOMIC_REFCOUNT  "Atomic refcounts (OFF: plain counters, single-threaded use only)" ON)
option(MOIRA_SHIPPING         "Freeze config and compile out dev instrumentation" OFF)
option(MOIRA_BUILD_TOOLS      "Build command-line tools (moira_parse_corpus)" ${PROJECT_IS_TOP_LEVEL})
option(MOIRA_BUILD_BENCH      "Build benchmarks" OFF)
option(MOIRA_BUILD_EXAMPLES   "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(MOIRA_BUILD_FUZZERS    "Build libFuzzer targets (Clang only)" OFF)
option(MOIRA_USE_EASTL        "Internal containers from EASTL instead of the STL (D55, D61)" OFF)
set(MOIRA_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined")

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings and sanitizers for everything this project builds (not for consumers).
if(MSVC)
  # /Zc:preprocessor: the conforming preprocessor. The traditional one mangles raw string
  # literals inside macro arguments, such as doctest's CHECK(...).
  add_compile_options(/W4 /permissive- /utf-8 /Zc:preprocessor)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)  # fopen is fine; we check every result
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
if(MOIRA_BUILD_FUZZERS)
  # Coverage instrumentation for everything the fuzzer exercises, not just its main.
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()
# Moira never throws, and everything here builds with exceptions disabled (D54).
if(MSVC)
  string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  add_compile_options(/EHs-c-)
  add_compile_definitions(_HAS_EXCEPTIONS=0)
else()
  add_compile_options(-fno-exceptions)
endif()

# Whether the library's Expected<T> is std::expected, decided once at configure time and
# exported, so code built at a different standard cannot silently disagree (D21).
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([[
#include <version>
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error no std::expected
#endif
int main() { return 0; }
]] MOIRA_HAVE_STD_EXPECTED)
if(MOIRA_HAVE_STD_EXPECTED)
  set(MOIRA_STD_EXPECTED_VALUE 1)
else()
  set(MOIRA_STD_EXPECTED_VALUE 0)
endif()
if(MOIRA_SANITIZE)
  # -fno-sanitize-recover: undefined behavior aborts, so tests and the fuzzer fail on it.
  add_compile_options(-fsanitize=${MOIRA_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${MOIRA_SANITIZE})
endif()

# EASTL for src/'s containers (src/common/containers.hpp). A host that builds EASTL itself
# provides the EASTL target first; otherwise it is fetched at a pinned commit, never vendored.
if(MOIRA_USE_EASTL AND NOT TARGET EASTL)
  include(FetchContent)
  FetchContent_Declare(EASTL
    GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
    GIT_TAG        e323f67e5d93fdadcd44f7998069d804697b1945)  # 3.27.01
  FetchContent_MakeAvailable(EASTL)
  # Its headers are not ours to warn about, and its sources are not ours to fix.
  foreach(t EASTL EABase)
    if(TARGET ${t})
      get_target_property(dirs ${t} INTERFACE_INCLUDE_DIRECTORIES)
      set_target_properties(${t} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
    endif()
  endforeach()
  set_target_properties(EASTL PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
  target_compile_options(EASTL PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
endif()

function(moira_configure_library target)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  if(MOIRA_USE_EASTL)
    target_compile_definitions(${target} PRIVATE MOIRA_USE_EASTL=1)
    target_link_libraries(${target} PRIVATE EASTL)
  endif()
  # Always explicit: mixing atomic and plain refcounts across translation units is an ODR
  # violation, so consumers must see the library's choice.
  if(MOIRA_ATOMIC_REFCOUNT)
    target_compile_definitions(${target} PUBLIC MOIRA_ATOMIC_REFCOUNT=1)
  else()
    target_compile_definitions(${target} PUBLIC MOIRA_ATOMIC_REFCOUNT=0)
  endif()
  target_compile_definitions(${target} PUBLIC MOIRA_STD_EXPECTED=${MOIRA_STD_EXPECTED_VALUE})
  foreach(flag MOIRA_SHIPPING)
    if(${flag})
      target_compile_definitions(${target} PUBLIC ${flag}=1)
    endif()
  endforeach()
endfunction()

# --- moira::data: nodes, arrays, parsing, lookup, config, tweaks, binary cache.
#     Must never depend on moira::script.
add_library(moira_data
  src/data/allocator.cpp
  src/data/arena.cpp
  src/data/clock.cpp
  src/data/array.cpp
  src/data/config.cpp
  src/data/error.cpp
  src/data/lookup.cpp
  src/data/merge.cpp
  src/data/node.cpp
  src/data/parse.cpp
  src/data/source_map.cpp
  src/data/symbol.cpp
  src/data/text.cpp
  src/data/tweak.cpp
  src/data/version.cpp
  src/data/write.cpp)
add_library(moira::data ALIAS moira_data)
moira_configure_library(moira_data)
# fast_float parses doubles identically on every platform (libc++ only gained double
# std::from_chars in LLVM 20). Private: it never appears in Moira's headers.
target_include_directories(moira_data SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/fast_float)

# --- moira::script: evaluation, functions, objects, messages, tasks.
if(MOIRA_BUILD_SCRIPT)
  add_library(moira_script
    src/script/builtins.cpp
    src/script/context.cpp
    src/script/object.cpp
    src/script/reference.cpp
    src/script/script_tasks.cpp
    src/script/task.cpp)
  add_library(moira::script ALIAS moira_script)
  moira_configure_library(moira_script)
  target_link_libraries(moira_script PUBLIC moira::data)
endif()

# --- moira::moira: umbrella.
add_library(moira INTERFACE)
add_library(moira::moira ALIAS moira)
target_link_libraries(moira INTERFACE moira::data)
if(MOIRA_BUILD_SCRIPT)
  target_link_libraries(moira INTERFACE moira::script)
endif()

if(MOIRA_BUILD_TESTS OR MOIRA_BUILD_FUZZERS)
  # Shared by the corpus tests and the fuzzers.
  add_library(moira_parse_harness STATIC fuzz/parse_harness.cpp fuzz/lookup_harness.cpp)
  target_include_directories(moira_parse_harness PUBLIC fuzz)
  target_link_libraries(moira_parse_harness PUBLIC moira::data)
endif()

if(MOIRA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MOIRA_BUILD_TOOLS)
  add_executable(moira_parse_corpus tools/parse_corpus.cpp)
  target_link_libraries(moira_parse_corpus PRIVATE moira::data)
endif()

if(MOIRA_BUILD_EXAMPLES)
  add_executable(moira_example_one_line_tweak examples/one_line_tweak/main.cpp)
  target_link_libraries(moira_example_one_line_tweak PRIVATE moira::data)
  if(MOIRA_BUILD_TESTS)
    add_test(NAME moira_example_one_line_tweak
      COMMAND moira_example_one_line_tweak ${PROJECT_SOURCE_DIR}/examples/one_line_tweak/config.dta)
    set_tests_properties(moira_example_one_line_tweak PROPERTIES
      PASS_REGULAR_EXPRESSION "before loading: box width 2[^.0-9].*after loading:  box width 3[.]5 .*config[.]dta:3[)]  note: 3-4 reads well")
  endif()
endif()

if(MOIRA_BUILD_EXAMPLES AND MOIRA_BUILD_SCRIPT)
  add_executable(moira_example_calculator examples/calculator/main.cpp)
  target_link_libraries(moira_example_calculator PRIVATE moira::script)
  if(MOIRA_BUILD_TESTS)
    add_test(NAME moira_example_calculator
      COMMAND moira_example_calculator --functions ${PROJECT_SOURCE_DIR}/examples/calculator/functions.dta
        "{+ 1 {* 2 3}}" "{square 7}" "{hypot 3 4}" "{average $ans 1}" "{clamp 12 0 10}")
    set_tests_properties(moira_example_calculator PROPERTIES
      PASS_REGULAR_EXPRESSION "^7\r?\n49\r?\n5\.0\r?\n3\.0\r?\n10\r?\n$")
  endif()
endif()

if(MOIRA_BUILD_BENCH)
  add_executable(moira_bench_parse bench/bench_parse.cpp tests/support/alloc_counter.cpp)
  target_include_directories(moira_bench_parse PRIVATE tests/support)
  target_link_libraries(moira_bench_parse PRIVATE moira::data)
  add_executable(moira_bench_lookup bench/bench_lookup.cpp)
  target_link_libraries(moira_bench_lookup PRIVATE moira::data)
  if(MOIRA_BUILD_SCRIPT)
    add_executable(moira_bench_tasks bench/bench_tasks.cpp tests/support/alloc_counter.cpp)
    target_include_directories(moira_bench_tasks PRIVATE tests/support)
    target_link_libraries(moira_bench_tasks PRIVATE moira::script)
    add_executable(moira_bench_call bench/bench_call.cpp tests/support/alloc_counter.cpp)
    target_include_directories(moira_bench_call PRIVATE tests/support)
    target_link_libraries(moira_bench_call PRIVATE moira::script)
  endif()
endif()

if(MOIRA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MOIRA_BUILD_FUZZERS requires Clang (libFuzzer)")
  endif()
  foreach(check parse lookup)
    add_executable(moira_${check}_fuzzer fuzz/libfuzzer_main.cpp)
    target_link_libraries(moira_${check}_fuzzer PRIVATE moira_parse_harness)
    target_compile_definitions(moira_${check}_fuzzer PRIVATE MOIRA_FUZZ_CHECK=check_${check})
    target_compile_options(moira_${check}_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(moira_${check}_fuzzer PRIVATE -fsanitize=fuzzer)
  endforeach()
endif()
