# Wayfarer (game-stacks.md §4.4): exploration missions in a world streamed by cell.
#   stacks_wayfarer_world   the world, missions, and the analyses (softlocks, reachability,
#                           pacing): no window and no Jolt, so tests link it alone
#   stacks_wayfarer         the game: the character on Jolt, streaming, the director, the map
add_library(stacks_wayfarer_world STATIC world.cpp)
target_include_directories(stacks_wayfarer_world PUBLIC .)
target_link_libraries(stacks_wayfarer_world PUBLIC stacks_nav moira::moira)
stacks_target(stacks_wayfarer_world)

add_executable(stacks_wayfarer main.cpp)
target_link_libraries(stacks_wayfarer PRIVATE stacks_wayfarer_world stacks_physics stacks_app)
target_compile_definitions(stacks_wayfarer PRIVATE WAYFARER_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_wayfarer)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_wayfarer_tests tests/test_world.cpp)
  target_link_libraries(stacks_wayfarer_tests PRIVATE stacks_wayfarer_world stacks_doctest_main)
  target_compile_definitions(stacks_wayfarer_tests PRIVATE
                             WAYFARER_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  stacks_target(stacks_wayfarer_tests)
  add_test(NAME stacks_wayfarer_tests COMMAND stacks_wayfarer_tests)
  add_test(NAME stacks_wayfarer_headless
           COMMAND stacks_wayfarer --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_wayfarer_sim COMMAND stacks_wayfarer sim --seeds 1)
  add_test(NAME stacks_wayfarer_check COMMAND stacks_wayfarer check)
endif()
