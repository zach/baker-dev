# Rows keyed by a path (dev/syntax-path-rows-design.md): a layer file is the diff it will
# become. Built only with -DBAKER_BUILD_EXPERIMENTS=ON.
add_executable(baker_syntax_path-rows syntax.cpp main.cpp)
target_link_libraries(baker_syntax_path-rows PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_path-rows_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_path-rows_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_path-rows_tests COMMAND baker_syntax_path-rows_tests)
endif()
