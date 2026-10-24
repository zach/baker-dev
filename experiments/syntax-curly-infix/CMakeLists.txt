# Curly-infix (dev/syntax-curly-infix-design.md). The rule itself is in the core's reader and
# printer; this directory holds the converter that rewrites DTA's operator calls as infix,
# the corpus checks, and the samples. It links baker::script to evaluate each rewrite against
# the command it replaces.
if(NOT TARGET baker::script)
  return()
endif()

add_executable(baker_syntax_curly-infix syntax.cpp main.cpp)
target_link_libraries(baker_syntax_curly-infix PRIVATE baker_experiments baker::script)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_curly-infix_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_curly-infix_tests PRIVATE baker_experiments baker::script
                                                               baker_doctest_main)
  add_test(NAME baker_syntax_curly-infix_tests COMMAND baker_syntax_curly-infix_tests)
endif()
