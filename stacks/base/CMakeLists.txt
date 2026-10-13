# The base (game-stacks.md §2): what every stack shares.
#
#   stacks_core   the live data loop, the watcher, the fixed step: no window, no renderer,
#                 so a headless runner and tests link it alone
#   stacks_app    the window (SDL3), the renderer (bgfx), Moira's panels (Dear ImGui), and
#                 its remote console; the frame loop that drives a Game
add_library(stacks_core STATIC src/live.cpp src/watcher.cpp)
target_include_directories(stacks_core PUBLIC include)
target_link_libraries(stacks_core PUBLIC moira::moira)
stacks_target(stacks_core)

# Moira's reference panels and its remote console, from its examples, as they are.
add_library(stacks_moira_tools STATIC
  ${MOIRA_ROOT}/examples/harness-imgui/panels.cpp
  ${MOIRA_ROOT}/examples/remote-console/console.cpp)
target_include_directories(stacks_moira_tools PUBLIC
  ${MOIRA_ROOT}/examples/harness-imgui ${MOIRA_ROOT}/examples/remote-console)
target_link_libraries(stacks_moira_tools PUBLIC moira::moira stacks_imgui)
if(WIN32)
  target_link_libraries(stacks_moira_tools PUBLIC ws2_32)
endif()

add_library(stacks_app STATIC src/app.cpp src/render.cpp)
target_include_directories(stacks_app PRIVATE ${BGFX_SHADERS_DIR})
target_link_libraries(stacks_app PUBLIC stacks_core PRIVATE stacks_moira_tools stacks_imgui bgfx bimg bx
                      SDL3::SDL3-static)
stacks_target(stacks_app)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_base_tests tests/test_fixed_step.cpp tests/test_watcher.cpp
                 tests/test_live.cpp)
  target_link_libraries(stacks_base_tests PRIVATE stacks_core stacks_doctest_main)
  stacks_target(stacks_base_tests)
  add_test(NAME stacks_base_tests COMMAND stacks_base_tests)
endif()
