# Third-party code for the stacks, fetched at pinned releases (stacks/THIRD_PARTY.md).
# None of it is ours to warn about: each target's warnings are off, and SYSTEM makes its
# headers external, so our warnings-as-errors builds do not fail on them (MSVC's C4324 in
# bx/file.h, for one).
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
  GIT_SHALLOW    TRUE
  SYSTEM)
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
  GIT_SUBMODULES_RECURSE TRUE
  SYSTEM)
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

# Dear ImGui (MIT), at the release Baker's reference harness uses.
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

# Jolt Physics (MIT): rigid bodies, constraints, vehicles, for the stacks that need physics
# (game-stacks.md §2.1). Built cross-platform deterministic, so a run replays bit for bit on
# any compiler and CPU the stacks build with (§3.1, §7); single precision; no exceptions and
# no RTTI-dependent features beyond its own.
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)  # keep the project's flags and runtime
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)  # CMAKE_MSVC_RUNTIME_LIBRARY decides
set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
FetchContent_Declare(jolt
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG        v5.5.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  Build
  SYSTEM)
FetchContent_MakeAvailable(jolt)
set_target_properties(Jolt PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(Jolt PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
# Floating point that replays: no fused multiply-add contractions, no fast math, for Jolt and
# for every target whose arithmetic feeds a simulation (stacks_deterministic). Jolt's own
# CMake sets these only when it overrides the project's flags, which we do not let it do.
function(stacks_deterministic target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:precise)
  else()
    target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
  endif()
endfunction()
stacks_deterministic(Jolt)

# Recast/Detour (zlib): navigation meshes and path queries, for the stacks whose characters
# walk (Hearth, Wayfarer; game-stacks.md §2.1). Only the two libraries: no demo, no tests.
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(recastnavigation
  GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
  GIT_TAG        v1.6.0
  GIT_SHALLOW    TRUE
  SYSTEM)
FetchContent_MakeAvailable(recastnavigation)
foreach(t Recast Detour DetourCrowd DetourTileCache DebugUtils)
  if(TARGET ${t})
    set_target_properties(${t} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
    target_compile_options(${t} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
    stacks_deterministic(${t})
  endif()
endforeach()
