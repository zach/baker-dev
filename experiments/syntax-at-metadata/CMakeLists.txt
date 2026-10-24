# The at-metadata experiment (dev/syntax-at-metadata-design.md): a tagged child whose tag
# begins with '@' is metadata. The core change is behind -DBAKER_AT_METADATA=ON; the
# translator here converts and round-trips the corpus with the switch on or off, and its
# tests of the core's own behaviour run only with it on.
add_executable(baker_syntax_at-metadata syntax.cpp main.cpp)
target_link_libraries(baker_syntax_at-metadata PRIVATE baker_experiments)

if(TARGET baker_doctest_main)
  add_executable(baker_syntax_at-metadata_tests syntax.cpp tests.cpp)
  target_link_libraries(baker_syntax_at-metadata_tests PRIVATE baker_experiments baker_doctest_main)
  # The tests of the shape grammar and of {func}'s metadata need those libraries.
  if(TARGET baker::shape)
    target_link_libraries(baker_syntax_at-metadata_tests PRIVATE baker::shape)
    target_compile_definitions(baker_syntax_at-metadata_tests PRIVATE BAKER_AT_SHAPE=1)
  endif()
  if(TARGET baker::script)
    target_link_libraries(baker_syntax_at-metadata_tests PRIVATE baker::script)
    target_compile_definitions(baker_syntax_at-metadata_tests PRIVATE BAKER_AT_SCRIPT=1)
  endif()
  target_include_directories(baker_syntax_at-metadata_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  add_test(NAME baker_syntax_at-metadata_tests COMMAND baker_syntax_at-metadata_tests)
endif()
