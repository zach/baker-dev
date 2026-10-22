# Indentation is structure (dev/syntax-indent-lines-design.md): a line that begins with an
# atom is an array. Built only with -DBAKER_BUILD_EXPERIMENTS=ON.
add_executable(baker_syntax_indent-lines syntax.cpp main.cpp)
target_link_libraries(baker_syntax_indent-lines PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_indent-lines_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_indent-lines_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_indent-lines_tests COMMAND baker_syntax_indent-lines_tests)
endif()
