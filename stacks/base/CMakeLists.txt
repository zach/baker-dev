# The base (game-stacks.md §2): what every stack shares.
#
#   stacks_core   the live data loop, the watcher, the fixed step, the headless simulation:
#                 no window, no renderer, so tests link it alone
#   stacks_app    the window (SDL3), the renderer (bgfx), Baker's panels (Dear ImGui), and
#                 the host kit's console (baker::host); the frame loop that drives a Game;
#                 the agent's commands
# Header-only: the words every stack wrote again, and the simulations' RNG (D115).
add_library(stacks_words INTERFACE)
target_include_directories(stacks_words INTERFACE include)
target_link_libraries(stacks_words INTERFACE baker::baker)

add_library(stacks_core STATIC src/live.cpp src/sim.cpp src/watcher.cpp)
target_include_directories(stacks_core PUBLIC include)
target_link_libraries(stacks_core PUBLIC baker::baker)
stacks_target(stacks_core)

# Jolt, deterministic (game-stacks.md §2.1): for the stacks with rigid bodies or vehicles.
add_library(stacks_physics STATIC src/physics.cpp)
target_include_directories(stacks_physics PUBLIC include)
target_link_libraries(stacks_physics PUBLIC Jolt)
stacks_target(stacks_physics)
stacks_deterministic(stacks_physics)

# Recast/Detour (game-stacks.md §2.1): navigation meshes and paths, for the stacks that walk.
add_library(stacks_nav STATIC src/nav.cpp)
target_include_directories(stacks_nav PUBLIC include)
target_link_libraries(stacks_nav PRIVATE Recast Detour)
stacks_target(stacks_nav)
stacks_deterministic(stacks_nav)

# Baker's reference panels, from its examples, as they are.
add_library(stacks_baker_tools STATIC ${BAKER_ROOT}/examples/harness-imgui/panels.cpp)
target_include_directories(stacks_baker_tools PUBLIC ${BAKER_ROOT}/examples/harness-imgui)
target_link_libraries(stacks_baker_tools PUBLIC baker::baker stacks_imgui)

add_library(stacks_app STATIC src/app.cpp src/render.cpp src/tools.cpp)
target_include_directories(stacks_app PRIVATE ${BGFX_SHADERS_DIR})
# SDL3 and Dear ImGui are public: a game reads its input from SDL's events and draws its own
# windows (Game::ui, Game::tools).
target_link_libraries(stacks_app PUBLIC stacks_core stacks_imgui SDL3::SDL3-static
                      PRIVATE stacks_baker_tools baker::host bgfx bimg bx)
stacks_target(stacks_app)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_base_tests tests/test_fixed_step.cpp tests/test_watcher.cpp
                 tests/test_live.cpp tests/test_sim.cpp tests/test_physics.cpp
                 tests/test_nav.cpp)
  target_link_libraries(stacks_base_tests PRIVATE stacks_core stacks_physics stacks_nav
                        stacks_doctest_main)
  stacks_target(stacks_base_tests)
  add_test(NAME stacks_base_tests COMMAND stacks_base_tests)
endif()
