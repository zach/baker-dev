# The identity experiment (experiments/README.md). Copy this directory to start one, and
# rename `template` in the three target names below.
add_executable(baker_syntax_template syntax.cpp main.cpp)
target_link_libraries(baker_syntax_template PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_template_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_template_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_template_tests COMMAND baker_syntax_template_tests)
endif()
