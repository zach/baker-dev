cmake_minimum_required(VERSION 3.20)
project(moira VERSION 0.0.1 LANGUAGES CXX)

option(MOIRA_BUILD_SCRIPT     "Build moira::script (evaluation, objects, tasks)" ON)
option(MOIRA_BUILD_TESTS      "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(MOIRA_ATOMIC_REFCOUNT  "Use atomic refcounts (multithreaded loading)" OFF)
option(MOIRA_SHIPPING         "Freeze config and compile out dev instrumentation" OFF)
option(MOIRA_NO_EXCEPTIONS    "Compile out throwing APIs; non-throwing twins only" OFF)
option(MOIRA_BUILD_TOOLS      "Build command-line tools (moira_parse_corpus)" ${PROJECT_IS_TOP_LEVEL})
option(MOIRA_BUILD_BENCH      "Build benchmarks" OFF)
option(MOIRA_BUILD_FUZZERS    "Build libFuzzer targets (Clang only)" OFF)
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
if(MOIRA_NO_EXCEPTIONS AND NOT MSVC)
  add_compile_options(-fno-exceptions)
endif()
if(MOIRA_SANITIZE)
  # -fno-sanitize-recover: undefined behavior aborts, so tests and the fuzzer fail on it.
  add_compile_options(-fsanitize=${MOIRA_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${MOIRA_SANITIZE})
endif()

function(moira_configure_library target)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_compile_features(${target} PUBLIC cxx_std_20)
  foreach(flag MOIRA_ATOMIC_REFCOUNT MOIRA_SHIPPING MOIRA_NO_EXCEPTIONS)
    if(${flag})
      target_compile_definitions(${target} PUBLIC ${flag}=1)
    endif()
  endforeach()
endfunction()

# --- moira::data: nodes, arrays, parsing, lookup, config, tweaks, binary cache.
#     Must never depend on moira::script.
add_library(moira_data
  src/data/array.cpp
  src/data/error.cpp
  src/data/node.cpp
  src/data/parse.cpp
  src/data/symbol.cpp
  src/data/text.cpp
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
    src/script/script_info.cpp)
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
  # Shared by the corpus test and the fuzzer.
  add_library(moira_parse_harness STATIC fuzz/parse_harness.cpp)
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

if(MOIRA_BUILD_BENCH)
  add_executable(moira_bench_parse bench/bench_parse.cpp)
  target_link_libraries(moira_bench_parse PRIVATE moira::data)
endif()

if(MOIRA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MOIRA_BUILD_FUZZERS requires Clang (libFuzzer)")
  endif()
  add_executable(moira_parse_fuzzer fuzz/libfuzzer_main.cpp)
  target_link_libraries(moira_parse_fuzzer PRIVATE moira_parse_harness)
  target_compile_options(moira_parse_fuzzer PRIVATE -fsanitize=fuzzer)
  target_link_options(moira_parse_fuzzer PRIVATE -fsanitize=fuzzer)
endif()
