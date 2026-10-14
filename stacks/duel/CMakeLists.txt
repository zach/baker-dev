# Duel (game-stacks.md §4.6): player versus computer fighting, on frame data.
#   stacks_duel_fight   the fight, in integers: no window, so its tests link it alone
#   stacks_duel         the game, and its agent commands (stacks/agent.md)
add_library(stacks_duel_fight STATIC fight.cpp)
target_include_directories(stacks_duel_fight PUBLIC .)
target_link_libraries(stacks_duel_fight PUBLIC moira::moira)
stacks_target(stacks_duel_fight)

add_executable(stacks_duel main.cpp)
target_link_libraries(stacks_duel PRIVATE stacks_duel_fight stacks_app)
target_compile_definitions(stacks_duel PRIVATE DUEL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_duel)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_duel_tests tests/test_fight.cpp)
  target_link_libraries(stacks_duel_tests PRIVATE stacks_duel_fight stacks_doctest_main)
  stacks_target(stacks_duel_tests)
  add_test(NAME stacks_duel_tests COMMAND stacks_duel_tests)
  add_test(NAME stacks_duel_headless
           COMMAND stacks_duel --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_duel_sim COMMAND stacks_duel sim --seeds 1-3)
  add_test(NAME stacks_duel_check COMMAND stacks_duel check)
endif()
