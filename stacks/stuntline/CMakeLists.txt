# Stuntline (game-stacks.md §4.5): stunt races on tracks laid from a kit, on Jolt's vehicle.
#   stacks_stuntline_race   tracks, the car, the drivers, the analyses: no window
#   stacks_stuntline        the game, and its agent commands (stacks/agent.md)
add_library(stacks_stuntline_race STATIC race.cpp)
target_include_directories(stacks_stuntline_race PUBLIC .)
target_link_libraries(stacks_stuntline_race PUBLIC stacks_physics baker::baker)
stacks_target(stacks_stuntline_race)
stacks_deterministic(stacks_stuntline_race)

add_executable(stacks_stuntline main.cpp)
target_link_libraries(stacks_stuntline PRIVATE stacks_stuntline_race stacks_app)
target_compile_definitions(stacks_stuntline PRIVATE STUNTLINE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_stuntline)
stacks_deterministic(stacks_stuntline)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_stuntline_tests tests/test_race.cpp)
  target_link_libraries(stacks_stuntline_tests PRIVATE stacks_stuntline_race stacks_doctest_main)
  stacks_target(stacks_stuntline_tests)
  add_test(NAME stacks_stuntline_tests COMMAND stacks_stuntline_tests)
  add_test(NAME stacks_stuntline_headless
           COMMAND stacks_stuntline --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_stuntline_sim COMMAND stacks_stuntline sim --seeds 1-2)
  add_test(NAME stacks_stuntline_check COMMAND stacks_stuntline check)
endif()
