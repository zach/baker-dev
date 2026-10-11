# Moira

A small, MIT-licensed C++20 data and scripting library in the spirit of Harmonix's
DataArray, as described in Eric Malafeew's GDC 2005 talk *Data-Driven Programming Made
Easy*.

The goal: replacing a hardcoded constant with a configured, hot-reloadable,
designer-editable value should cost one line and no ceremony.

## Status

**M1: data representation, parser, preprocessor, printer.** You can read DTA text into
trees, inspect them with checked accessors, and print them back. Lookup by name, config,
tweaks, and scripting are later milestones (`docs/spec.md` §10). `docs/decisions.md`
records where the implementation deviates from the spec.

```cpp
#include <moira/moira.hpp>

auto menu = moira::read_file("menu.dta");          // or try_read_file(): no exceptions
for (const moira::Node& item : *menu) {
    const moira::Array* a = item.try_array();       // raw access; never evaluates
    if (a && !a->empty()) std::printf("%s\n", moira::to_string(*a).c_str());
}
std::string text = moira::to_dta(*menu);            // round-trips through read_string()
```

What exists:

- `Node` (16 bytes), interned `Symbol`, refcounted `String` and `Array` (one allocation
  per array, children inline), every array tagged with its source file and line.
- A parser that never crashes on bad input: every error has a file and line.
- The preprocessor: `#include`, `#include_opt`, `#merge`, `#ifdef`/`#ifndef`/`#else`/
  `#endif`, `#define`, `#undef`, `#autorun`, and `[NAME ...]` macros.
- A pretty-printer whose output parses back to an equal tree.
- Tests, a hand-written corpus, and a libFuzzer harness (`-DMOIRA_BUILD_FUZZERS=ON`).

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
| `MOIRA_BUILD_BENCH` | `OFF` | `moira_bench_parse` |
| `MOIRA_BUILD_FUZZERS` | `OFF` | `moira_parse_fuzzer` (Clang) |

To check the parser against a local DTA corpus (none is included; see
`docs/decisions.md` D3):

```sh
build/moira_parse_corpus --roundtrip path/to/corpus
```

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
