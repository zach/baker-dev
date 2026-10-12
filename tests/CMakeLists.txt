add_library(moira_doctest_main STATIC doctest_main.cpp)
target_include_directories(moira_doctest_main SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_features(moira_doctest_main PUBLIC cxx_std_20)
# doctest 2.4.11 forward-declares std::basic_ostream, which newer MSVC STLs (14.44+)
# reject; include the real standard headers instead.
target_compile_definitions(moira_doctest_main PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
# Built without exceptions (D54): REQUIRE terminates instead of throwing.
target_compile_definitions(moira_doctest_main PUBLIC DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

add_executable(moira_data_tests
  data/test_arena.cpp
  data/test_config.cpp
  data/test_core.cpp
  data/test_corpus.cpp
  data/test_lookup.cpp
  data/test_m3b.cpp
  data/test_parse.cpp
  data/test_preprocessor.cpp
  data/test_version.cpp
  data/test_write.cpp
  readiness_loop.cpp
  support/alloc_counter.cpp)
target_link_libraries(moira_data_tests PRIVATE moira::data moira_parse_harness moira_doctest_main)
# White-box tests of internal helpers (text.hpp).
target_include_directories(moira_data_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/data support)
if(MOIRA_USE_EASTL)  # the white-box tests include src/data/text.hpp
  target_compile_definitions(moira_data_tests PRIVATE MOIRA_USE_EASTL=1)
  target_link_libraries(moira_data_tests PRIVATE EASTL)
endif()
target_compile_definitions(moira_data_tests PRIVATE
  MOIRA_CORPUS_DIR="${PROJECT_SOURCE_DIR}/tests/corpus"
  MOIRA_FUZZ_REGRESSIONS_DIR="${PROJECT_SOURCE_DIR}/tests/fuzz_regressions")
add_test(NAME moira_data_tests COMMAND moira_data_tests)

if(MOIRA_BUILD_SCRIPT)
  add_executable(moira_script_tests
    script/test_binding.cpp
    script/test_check.cpp
    script/test_debug.cpp
    script/test_eval.cpp
    script/test_messages.cpp
    script/test_object.cpp
    script/test_stats.cpp
    support/alloc_counter.cpp)
  target_link_libraries(moira_script_tests PRIVATE moira::script moira_doctest_main)
  target_include_directories(moira_script_tests PRIVATE support)
  target_compile_definitions(moira_script_tests PRIVATE
    MOIRA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MOIRA_CORPUS_DIR="${PROJECT_SOURCE_DIR}/tests/corpus")
  add_test(NAME moira_script_tests COMMAND moira_script_tests)
endif()

if(MOIRA_BUILD_SCRIPT)
  # D61: its own binary, because main installs the default allocator before anything runs.
  add_executable(moira_alloc_tests alloc/main.cpp alloc/test_allocators.cpp support/alloc_counter.cpp)
  target_link_libraries(moira_alloc_tests PRIVATE moira::script)
  target_include_directories(moira_alloc_tests PRIVATE support)
  target_include_directories(moira_alloc_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
  target_compile_definitions(moira_alloc_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS
                             DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
  add_test(NAME moira_alloc_tests COMMAND moira_alloc_tests)
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

# D55: public headers reach only the allowed standard headers (std_*.hpp excepted), and
# each compiles on its own.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME moira_header_allowlist
           COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_headers.py)
endif()
file(GLOB moira_public_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/moira/*.hpp)
set(moira_header_units)
foreach(header ${moira_public_headers})
  get_filename_component(name ${header} NAME_WE)
  set(unit ${CMAKE_CURRENT_BINARY_DIR}/header_alone/${name}.cpp)
  file(WRITE ${unit} "#include <moira/${name}.hpp>\n")
  list(APPEND moira_header_units ${unit})
endforeach()
add_library(moira_header_alone OBJECT ${moira_header_units})
if(MOIRA_BUILD_SCRIPT)
  target_link_libraries(moira_header_alone PRIVATE moira::script)
else()
  target_link_libraries(moira_header_alone PRIVATE moira::data)
endif()
