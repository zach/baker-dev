# Layer verbs beside #remove (dev/syntax-layer-markers-design.md): the core reads the
# verbs; this directory converts layers to them and measures them.
add_executable(baker_syntax_layer-markers syntax.cpp main.cpp)
target_link_libraries(baker_syntax_layer-markers PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_layer-markers_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_layer-markers_tests PRIVATE baker_experiments baker_doctest_main)
  add_test(NAME baker_syntax_layer-markers_tests COMMAND baker_syntax_layer-markers_tests)
endif()
