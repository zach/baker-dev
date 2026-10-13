# Third-party code in the stacks

Fetched by CMake at configure time, at pinned releases (`cmake/dependencies.cmake`);
nothing is vendored. Moira's own (`../THIRD_PARTY.md`) applies as well.

| Component | Version | License | Used for |
|-----------|---------|---------|----------|
| [SDL](https://github.com/libsdl-org/SDL) | 3.2.30 | zlib | Window, input, gamepads |
| [bgfx](https://github.com/bkaradzic/bgfx) with [bx](https://github.com/bkaradzic/bx) and [bimg](https://github.com/bkaradzic/bimg) | as [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) v1.166.9546-589 pins them | BSD-2-Clause | Rendering; PNG screenshots (bimg) |
| bgfx's precompiled example shaders (`debugdraw/*_fill_lit*`, `imgui/*_ocornut_imgui`) | as above | BSD-2-Clause | The box and ImGui shaders, compiled into `stacks_app` |
| [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) | v1.166.9546-589 | CC0-1.0 | bgfx's CMake build |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.9b (`f1cc2ae`) | MIT | Tuning panels (and its SDL3 platform backend) |
