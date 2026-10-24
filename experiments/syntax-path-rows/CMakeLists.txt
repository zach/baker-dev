# Rows keyed by a path (dev/syntax-path-rows-design.md): a layer file is the diff it will
# become. Built only with -DBAKER_BUILD_EXPERIMENTS=ON.
add_executable(baker_syntax_path-rows syntax.cpp main.cpp)
target_link_libraries(baker_syntax_path-rows PRIVATE baker_experiments)
if(TARGET baker_alloc_counter)  # `count` reports a read's allocations (the arena's blocks)
  target_link_libraries(baker_syntax_path-rows PRIVATE baker_alloc_counter)
  target_compile_definitions(baker_syntax_path-rows PRIVATE BAKER_SYNTAX_ALLOCS=1)
endif()

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_path-rows_tests syntax.cpp tests.cpp tests_config.cpp)
  target_link_libraries(baker_syntax_path-rows_tests PRIVATE baker_experiments baker_doctest_main
                        baker_parse_harness)
  # TempDir; the fuzz harness's invariants, run over rows.
  target_include_directories(baker_syntax_path-rows_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  add_test(NAME baker_syntax_path-rows_tests COMMAND baker_syntax_path-rows_tests)
endif()
