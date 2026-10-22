# Contraption (game-stacks.md §4.1): physics puzzles, side on, on Jolt.
#   stacks_contraption_puzzle   the puzzles and their analyses: no window, so tests link it alone
#   stacks_contraption          the game, and its agent commands (stacks/agent.md)
add_library(stacks_contraption_puzzle STATIC puzzle.cpp)
target_include_directories(stacks_contraption_puzzle PUBLIC .)
target_link_libraries(stacks_contraption_puzzle PUBLIC stacks_physics baker::baker)
stacks_target(stacks_contraption_puzzle)
stacks_deterministic(stacks_contraption_puzzle)

add_executable(stacks_contraption main.cpp)
target_link_libraries(stacks_contraption PRIVATE stacks_contraption_puzzle stacks_app)
target_compile_definitions(stacks_contraption PRIVATE
                           CONTRAPTION_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_contraption)
stacks_reference(stacks_contraption)
stacks_deterministic(stacks_contraption)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_contraption_tests tests/test_puzzle.cpp)
  target_link_libraries(stacks_contraption_tests PRIVATE stacks_contraption_puzzle
                        stacks_doctest_main)
  stacks_target(stacks_contraption_tests)
  add_test(NAME stacks_contraption_tests COMMAND stacks_contraption_tests)
  add_test(NAME stacks_contraption_headless
           COMMAND stacks_contraption --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_contraption_sim COMMAND stacks_contraption sim --baker-seeds 1-2)
  add_test(NAME stacks_contraption_check COMMAND stacks_contraption check)
endif()
