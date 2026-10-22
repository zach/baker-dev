# The identity experiment (experiments/README.md). Copy this directory to start one, and
# rename `column-tables` in the three target names below.
add_executable(baker_syntax_column-tables syntax.cpp main.cpp)
target_link_libraries(baker_syntax_column-tables PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_column-tables_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_column-tables_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_column-tables_tests COMMAND baker_syntax_column-tables_tests)
endif()
