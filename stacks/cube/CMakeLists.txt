# The base's acceptance game (game-stacks.md §6, step 1): a cube and its tweaks.
add_executable(stacks_cube main.cpp)
target_link_libraries(stacks_cube PRIVATE stacks_app)
target_compile_definitions(stacks_cube PRIVATE CUBE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_cube)
if(WIN32)
  set_target_properties(stacks_cube PROPERTIES WIN32_EXECUTABLE OFF)  # keep the console
endif()

if(STACKS_BUILD_TESTS)
  # A headless run: the data loads, the frame loop runs, the renderer starts and stops.
  add_test(NAME stacks_cube_headless
           COMMAND stacks_cube --headless --frames 120 --console-port 0 --no-vsync)
  # The agent's commands on the cube's own data, scenarios, and objectives.
  add_test(NAME stacks_cube_sim COMMAND stacks_cube sim --seeds 1-3)
  add_test(NAME stacks_cube_check COMMAND stacks_cube check)
endif()
