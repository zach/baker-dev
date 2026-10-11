# Moira

A small, MIT-licensed C++20 data and scripting library in the spirit of Harmonix's
DataArray, as described in Eric Malafeew's GDC 2005 talk *Data-Driven Programming Made
Easy*.

The goal: replacing a hardcoded constant with a configured, hot-reloadable,
designer-editable value should cost one line and no ceremony.

## Status

**M2: lookup.** You can read DTA text into trees, look values up by tag or by path with
typed, located errors, and print trees back. Config, tweaks, and scripting are later
milestones (`docs/spec.md` §10). `docs/decisions.md` records where the implementation
deviates from the spec.

```cpp
#include <moira/moira.hpp>

// (menu ('bacon and eggs' (price 12.75) (calories 120)))
auto root = moira::read_file("menu.dta");          // or try_read_file(): no exceptions

const moira::Array& eggs = root->find("menu").find("bacon and eggs");
double price = eggs.find_float("price");            // 12.75
int    fat   = eggs.find<int>("fat", 0);            // absent: the fallback
float  same  = root->get<float>("menu.'bacon and eggs'.price");

auto cals = eggs.try_find_int("calories");          // Expected<int64_t>, no exceptions
for (const moira::Array& item : root->find("menu").subarrays()) { /* ... */ }

std::string text = moira::to_dta(*root);            // round-trips through read_string()
```

A failed lookup says what, where, and how it got there:

```
moira: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
```

Read your own types by specializing `moira::Convert`:

```cpp
template <> struct moira::Convert<Vec2> {
    static constexpr std::string_view name = "vec2";
    static bool from(const moira::Values& v, Vec2& out) {
        return v.size() == 2 && v.read(0, out.x) && v.read(1, out.y);
    }
};
Vec2 size = root->get<Vec2>("renderer.screen_size");   // (screen_size 1920 1080)
```

What exists:

- `Node` (16 bytes; 64-bit ints and doubles), interned `Symbol`, refcounted `String` and `Array` (one allocation
  per array, children inline), every array tagged with its source file and line.
- A parser that never crashes on bad input: every error has a file and line.
- The preprocessor: `#include`, `#include_opt`, `#merge`, `#ifdef`/`#ifndef`/`#else`/
  `#endif`, `#define`, `#undef`, `#autorun`, and `[NAME ...]` macros.
- A pretty-printer whose output parses back to an equal tree.
- Lookup: `find`/`try_find` by tag, `find_int`/`find_float`/`find_sym`/`find_str`/
  `find_array`, fallbacks, `get<T>` by `Path`, `Convert<T>` for your own types (vectors and
  fixed arrays built in), opt-in `sort_for_lookup()` binary search, and `subarrays()`.
  `find_float` on a 20-child array takes about 10 ns and never allocates.
- Tests, a hand-written corpus, benchmarks, and libFuzzer harnesses for parsing and lookup
  (`-DMOIRA_BUILD_FUZZERS=ON`).

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Targets: `moira::data` (data layer, usable alone), `moira::script` (script layer,
depends on data), `moira::moira` (both). Build the data layer by itself with
`-DMOIRA_BUILD_SCRIPT=OFF`.

| Option | Default | Effect |
|--------|---------|--------|
| `MOIRA_BUILD_SCRIPT` | `ON` | Build `moira::script` |
| `MOIRA_BUILD_TESTS` | `ON` when top-level | Build the test suite |
| `MOIRA_ATOMIC_REFCOUNT` | `ON` | Atomic refcounts; `OFF` for single-threaded programs |
| `MOIRA_SHIPPING` | `OFF` | Freeze config; compile out dev instrumentation |
| `MOIRA_NO_EXCEPTIONS` | `OFF` | Non-throwing APIs only |
| `MOIRA_SANITIZE` | empty | e.g. `address,undefined` |
| `MOIRA_BUILD_TOOLS` | `ON` when top-level | `moira_parse_corpus` |
| `MOIRA_BUILD_BENCH` | `OFF` | `moira_bench_parse`, `moira_bench_lookup` |
| `MOIRA_BUILD_FUZZERS` | `OFF` | `moira_parse_fuzzer`, `moira_lookup_fuzzer` (Clang) |

Android: clang defaults to C++17. CMake builds get C++20 from the targets; with
`ndk-build`, add `APP_CPPFLAGS += -std=c++20`.

To check the parser against a local DTA corpus (none is included; see
`docs/decisions.md` D3):

```sh
build/moira_parse_corpus --roundtrip path/to/corpus
```

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
