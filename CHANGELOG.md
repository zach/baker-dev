# Changelog

Baker's releases, newest first. Versions follow [semantic versioning](https://semver.org);
before 1.0.0, a minor version may change the API.

## 0.1.0 - 2026-10-15

The first release. Baker reads a Lisp-like text format into trees, stacks files into layers
that reload while the program runs, and scripts behavior in the same syntax.

- **Data.** Parsing with macros, conditionals, includes and merges; lookups by tag and by
  path; typed reads with defaults; a printer that round-trips. Errors name the file and
  line.
- **Config.** Layers of files and overlays that reload on change, with rollback, provenance
  (`source`), live edits through `Tweak<T>`, layer placement (`below`/`above`), removal with
  `(key #remove)`, and write-back of edits to the files they came from.
- **Binary cache.** Trees written as they lie in memory and loaded with one allocation,
  laid out so that an edit to the text changes few bytes of the cache, for delta patches.
- **Packs.** Levels, ranks and the one-way rule; versions and versioned requirements;
  mods read as text only.
- **Shapes.** Declared structure for data, checked with located errors and warnings; a
  softer pack may narrow a harder pack's shape, not loosen it.
- **Script.** Functions, classes and objects, typed messages, tasks, a checker, call
  statistics, and autorun that follows the layer stack.
- **Builds.** C++20 on GCC 13, Clang 18 and MSVC 2022; a shipping mode that compiles out
  live editing; Baker's own allocators throughout, optionally EASTL containers.
