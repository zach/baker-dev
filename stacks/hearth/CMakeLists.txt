# Hearth (game-stacks.md §4.3): households of villagers with needs, smart objects, and actions
# as script.
#   stacks_hearth_sim   the households, the soak, and its measures: no window, so tests link it
#   stacks_hearth       the game, and its agent commands (stacks/agent.md)
add_library(stacks_hearth_sim STATIC household.cpp)
target_include_directories(stacks_hearth_sim PUBLIC .)
target_link_libraries(stacks_hearth_sim PUBLIC stacks_nav moira::moira)
stacks_target(stacks_hearth_sim)

add_executable(stacks_hearth main.cpp)
target_link_libraries(stacks_hearth PRIVATE stacks_hearth_sim stacks_app)
target_compile_definitions(stacks_hearth PRIVATE HEARTH_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_hearth)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_hearth_tests tests/test_household.cpp)
  target_link_libraries(stacks_hearth_tests PRIVATE stacks_hearth_sim stacks_doctest_main)
  stacks_target(stacks_hearth_tests)
  add_test(NAME stacks_hearth_tests COMMAND stacks_hearth_tests)
  add_test(NAME stacks_hearth_headless
           COMMAND stacks_hearth --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_hearth_sim COMMAND stacks_hearth sim --seeds 1)
  add_test(NAME stacks_hearth_check COMMAND stacks_hearth check)
endif()
