# Each example is a small program that CTest runs against its expected output (M9).

# A test that runs `target` with `args`, passing if its output matches `regex`. Examples
# run in a directory of their own under the build tree, so a run that writes files never
# touches the sources.
function(moira_example_test name target regex)
  set(dir ${CMAKE_CURRENT_BINARY_DIR}/run/${name})
  file(MAKE_DIRECTORY ${dir})
  add_test(NAME ${name} COMMAND ${target} ${ARGN} WORKING_DIRECTORY ${dir})
  set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

set(nl "\r?\n")
# Shipping builds make reload, live edits, and write-back inert, so examples that show
# them are tested only in development builds.
if(MOIRA_BUILD_TESTS AND NOT MOIRA_SHIPPING)
  set(moira_test_dev ON)
else()
  set(moira_test_dev OFF)
endif()

# --- quickstart: the README's five-minute path (D87 Q4).
add_executable(moira_example_quickstart quickstart/main.cpp)
target_link_libraries(moira_example_quickstart PRIVATE moira::data)
add_executable(moira_example_quickstart_test quickstart/test_quickstart.cpp)
target_link_libraries(moira_example_quickstart_test PRIVATE moira::data)
if(moira_test_dev)
  moira_example_test(moira_example_quickstart moira_example_quickstart_test
    "^player speed 4[.]5${nl}player speed 6${nl}moira: [^\n]*default[.]dta:1[^\n]*${nl}player speed 6${nl}$"
    ${CMAKE_CURRENT_SOURCE_DIR}/quickstart/default.dta)
endif()
if(MOIRA_BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME moira_readme_blocks
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_readme.py)
  endif()
endif()

# --- one_line_tweak: the constant replacement, before and after.
add_executable(moira_example_one_line_tweak one_line_tweak/main.cpp)
target_link_libraries(moira_example_one_line_tweak PRIVATE moira::data)
if(MOIRA_BUILD_TESTS)
  moira_example_test(moira_example_one_line_tweak moira_example_one_line_tweak
    "before loading: box width 2[^.0-9].*after loading:  box width 3[.]5 .*config[.]dta:3[)]  note: 3-4 reads well"
    ${CMAKE_CURRENT_SOURCE_DIR}/one_line_tweak/config.dta)
endif()

if(MOIRA_BUILD_SCRIPT)
  # --- calculator: eval, with native and script functions.
  add_executable(moira_example_calculator calculator/main.cpp)
  target_link_libraries(moira_example_calculator PRIVATE moira::script)
  if(MOIRA_BUILD_TESTS)
    moira_example_test(moira_example_calculator moira_example_calculator
      "^7${nl}49${nl}5\\.0${nl}3\\.0${nl}10${nl}$"
      --functions ${CMAKE_CURRENT_SOURCE_DIR}/calculator/functions.dta
      "{+ 1 {* 2 3}}" "{square 7}" "{hypot 3 4}" "{average $ans 1}" "{clamp 12 0 10}")
  endif()
endif()
