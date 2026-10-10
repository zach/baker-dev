add_library(moira_doctest_main STATIC doctest_main.cpp)
target_include_directories(moira_doctest_main SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_features(moira_doctest_main PUBLIC cxx_std_20)
# doctest 2.4.11 forward-declares std::basic_ostream, which newer MSVC STLs (14.44+)
# reject; include the real standard headers instead.
target_compile_definitions(moira_doctest_main PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)

add_executable(moira_data_tests
  data/test_version.cpp)
target_link_libraries(moira_data_tests PRIVATE moira::data moira_doctest_main)
add_test(NAME moira_data_tests COMMAND moira_data_tests)

if(MOIRA_BUILD_SCRIPT)
  add_executable(moira_script_tests
    script/test_script_info.cpp)
  target_link_libraries(moira_script_tests PRIVATE moira::script moira_doctest_main)
  add_test(NAME moira_script_tests COMMAND moira_script_tests)
endif()
