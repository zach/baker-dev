add_library(moira_doctest_main STATIC doctest_main.cpp)
target_include_directories(moira_doctest_main SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_features(moira_doctest_main PUBLIC cxx_std_20)
# doctest 2.4.11 forward-declares std::basic_ostream, which newer MSVC STLs (14.44+)
# reject; include the real standard headers instead.
target_compile_definitions(moira_doctest_main PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
if(MOIRA_NO_EXCEPTIONS)
  # Keep REQUIRE (it terminates instead of throwing).
  target_compile_definitions(moira_doctest_main PUBLIC DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
endif()

add_executable(moira_data_tests
  data/test_config.cpp
  data/test_core.cpp
  data/test_corpus.cpp
  data/test_lookup.cpp
  data/test_parse.cpp
  data/test_preprocessor.cpp
  data/test_version.cpp
  data/test_write.cpp
  support/alloc_counter.cpp)
target_link_libraries(moira_data_tests PRIVATE moira::data moira_parse_harness moira_doctest_main)
# White-box tests of internal helpers (text.hpp).
target_include_directories(moira_data_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/data support)
target_compile_definitions(moira_data_tests PRIVATE
  MOIRA_CORPUS_DIR="${PROJECT_SOURCE_DIR}/tests/corpus"
  MOIRA_FUZZ_REGRESSIONS_DIR="${PROJECT_SOURCE_DIR}/tests/fuzz_regressions")
add_test(NAME moira_data_tests COMMAND moira_data_tests)

if(MOIRA_BUILD_SCRIPT)
  add_executable(moira_script_tests
    script/test_script_info.cpp)
  target_link_libraries(moira_script_tests PRIVATE moira::script moira_doctest_main)
  add_test(NAME moira_script_tests COMMAND moira_script_tests)
endif()

# Spec §7: a Tweak<float> read is a single load. Checked in the generated assembly with
# GCC and Clang, unless sanitizers or fuzzing instrument every load.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MOIRA_SANITIZE AND NOT MOIRA_BUILD_FUZZERS)
  separate_arguments(moira_codegen_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
  add_test(NAME moira_tweak_codegen COMMAND ${CMAKE_COMMAND}
    -DCXX=${CMAKE_CXX_COMPILER}
    -DSTD=${CMAKE_CXX_STANDARD}
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/codegen/tweak_read.cpp
    -DINC=${PROJECT_SOURCE_DIR}/include
    "-DDEFS=-DMOIRA_STD_EXPECTED=${MOIRA_STD_EXPECTED_VALUE};-DMOIRA_ATOMIC_REFCOUNT=$<BOOL:${MOIRA_ATOMIC_REFCOUNT}>"
    "-DFLAGS=${moira_codegen_flags}"
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/tweak_read.s
    -DARCH=${CMAKE_SYSTEM_PROCESSOR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/codegen/check_tweak_read.cmake)
endif()
