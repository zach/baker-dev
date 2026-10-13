# Third-party code for the stacks, fetched at pinned releases (stacks/THIRD_PARTY.md).
# None of it is ours to warn about, so each target's warnings are off.
include(FetchContent)

# SDL3: window, input, gamepads (zlib). Static, with nothing we do not use.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)  # X11 only on Linux, where only tests and CI run
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)  # bgfx renders
FetchContent_Declare(sdl3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-3.2.30
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(sdl3)

# bgfx: rendering (BSD-2), through bgfx.cmake (CC0), which brings bx and bimg.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(bgfx
  GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
  GIT_TAG        v1.166.9546-589
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(bgfx)
foreach(t bx bimg bimg_decode bimg_encode bgfx)
  if(TARGET ${t})
    set_target_properties(${t} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
    target_compile_options(${t} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
  endif()
endforeach()
# bgfx's own precompiled shaders, for every renderer, ship as headers in its examples:
# the base uses the lit fill shader of its debug draw and the ImGui shader.
set(BGFX_SHADERS_DIR ${bgfx_SOURCE_DIR}/bgfx/examples/common)

# Dear ImGui (MIT), at the release Moira's reference harness uses.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        f1cc2ae15e53a861a874c3034aae6798fde194ab)  # v1.92.9b
FetchContent_MakeAvailable(imgui)
add_library(stacks_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(stacks_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(stacks_imgui PUBLIC SDL3::SDL3-static)
target_compile_options(stacks_imgui PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
