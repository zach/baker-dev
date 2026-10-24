# Column tables (README.md, dev/syntax-column-tables-design.md): a front end over Baker's
# public headers, built only with -DBAKER_BUILD_EXPERIMENTS=ON.
add_executable(baker_syntax_column-tables syntax.cpp main.cpp)
target_link_libraries(baker_syntax_column-tables PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_column-tables_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_column-tables_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_column-tables_tests COMMAND baker_syntax_column-tables_tests)
endif()

if(BAKER_BUILD_FUZZERS)
  # fuzz.cpp: the core fuzzer's property over this form's reader and printer.
  add_executable(baker_syntax_column-tables_fuzzer syntax.cpp fuzz.cpp)
  target_link_libraries(baker_syntax_column-tables_fuzzer PRIVATE baker_experiments)
  target_compile_options(baker_syntax_column-tables_fuzzer PRIVATE -fsanitize=fuzzer)
  target_link_options(baker_syntax_column-tables_fuzzer PRIVATE -fsanitize=fuzzer)
endif()
