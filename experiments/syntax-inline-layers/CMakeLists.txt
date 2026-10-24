# Layers inside a file (dev/syntax-inline-layers-design.md): the #ifdef-to-block converter,
# its check, and the benchmarks. Needs the core's BAKER_INLINE_LAYERS, which the experiments
# build turns on by default.
if(NOT BAKER_INLINE_LAYERS)
  message(STATUS "experiments/syntax-inline-layers needs -DBAKER_INLINE_LAYERS=ON; skipped")
  return()
endif()

add_executable(baker_syntax_inline-layers syntax.cpp convert.cpp main.cpp)
target_link_libraries(baker_syntax_inline-layers PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_inline-layers_tests syntax.cpp convert.cpp tests.cpp)
  target_link_libraries(baker_syntax_inline-layers_tests PRIVATE baker_experiments
                        baker_doctest_main)
  add_test(NAME baker_syntax_inline-layers_tests COMMAND baker_syntax_inline-layers_tests)
endif()
