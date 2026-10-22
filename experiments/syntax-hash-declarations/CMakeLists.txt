# The hash-declarations experiment (dev/syntax-hash-declarations-design.md).
add_executable(baker_syntax_hash-declarations syntax.cpp main.cpp)
target_link_libraries(baker_syntax_hash-declarations PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_hash-declarations_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_hash-declarations_tests PRIVATE baker_experiments baker_doctest_main)
  if(TARGET baker_shape)
    # The at-the-table #shape is proved with the shape library's checker.
    target_link_libraries(baker_syntax_hash-declarations_tests PRIVATE baker::shape)
    target_compile_definitions(baker_syntax_hash-declarations_tests PRIVATE BAKER_SYNTAX_SHAPE=1)
  endif()
  add_test(NAME baker_syntax_hash-declarations_tests COMMAND baker_syntax_hash-declarations_tests)
endif()
