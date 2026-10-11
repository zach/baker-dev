# Moira

A small, MIT-licensed C++20 data and scripting library in the spirit of Harmonix's
DataArray, as described in Eric Malafeew's GDC 2005 talk *Data-Driven Programming Made
Easy*.

The goal: replacing a hardcoded constant with a configured, hot-reloadable,
designer-editable value should cost one line and no ceremony.

## Status

**M5: config, tweaks, the data loop, script evaluation, and objects.** You can read DTA text into
trees, look values up with typed, located errors, stack config files and overlays into
layers, hot-reload them with every reload timed, replace constants with one-line tweaks,
toggle experiment layers A/B, snapshot and restore exact states, and write values tuned in
the running program back into their source files. Commands evaluate lazily, native
functions bind in one line, C++ objects are callable from script (and script handlers
from C++), and `moira::check` finds bad calls before anything runs. Messages and tasks are
later milestones (`docs/spec.md` §10).
`docs/decisions.md` records where the implementation deviates from the spec.

The one-line constant replacement (`examples/one_line_tweak`):

```cpp
// Before:
const float kBoxWidth = 2.0f;

// After: configured, hot-reloadable, editable.
static moira::Tweak<float> kBoxWidth{"box.width", 2.0f,
    {.range = {0, 10}, .step = 1, .desc = "Box width"}};

float w = kBoxWidth;      // one relaxed atomic load
```

```cpp
moira::Config cfg;                                  // binds every Tweak
cfg.push_layer("default", "default.dta");
cfg.push_layer("local", "local/sam.dta", {.optional = true});   // per user, not checked in
cfg.push_layer("patch", overlay);                   // a live-ops overlay array
cfg.remove_layer("patch");                          // rollback

cfg.on_reload("renderer", [&] { renderer.rebuild(); });   // when that group changes
cfg.reload({.trigger = "watcher"_sym, .origin_ns = saved_at});   // after a file changes
auto rec = cfg.last_reload();                       // scan, read, parse, merge, bind, notify

moira::Source s = cfg.source("box.width");          // layer, file, and line of the value
kBoxWidth.set(2.5f);                                // tune it live (the scratch layer)...
kBoxWidth.write_back();                             // ...and save it: a one-line diff

cfg.toggle_layer("experiment");                     // A/B
auto changes = cfg.layer_effect("experiment");      // what a layer actually changes
cfg.promote("experiment", "default");               // into shared data, all or nothing
auto snap = cfg.snapshot();                         // ...and later cfg.restore(snap)
```

Lookup on a tree:

```cpp
// (menu ('bacon and eggs' (price 12.75) (calories 120)))
auto root = moira::read_file("menu.dta");          // or try_read_file(): no exceptions

const moira::Array& eggs = root->find("menu").find("bacon and eggs");
double price = eggs.find_float("price");            // 12.75
int    fat   = eggs.find<int>("fat", 0);            // absent: the fallback
float  same  = root->get<float>("menu.'bacon and eggs'.price");
std::string text = moira::to_dta(*root);            // round-trips through read_string()
```

Scripting (`examples/calculator`):

```cpp
moira::Context ctx;
ctx.func("hypot", [](double a, double b) { return std::hypot(a, b); });  // types deduced
ctx.eval("{hypot 3 4}");                            // 5.0
ctx.eval("{func square ($x) {* $x $x}}");           // a script function
ctx.var("difficulty") = 3;

// (enemy (hp {* 100 $difficulty}))
int hp = enemy.find_int("hp");                      // 300: typed lookups evaluate
auto report = moira::check(*tree, ctx);             // wrong arity, found without running
```

`if` is an ordinary function: its branches are passed unevaluated, so the evaluator needs
no special forms (`docs/script-reference.md` lists every built-in).

Objects (`moira/object.hpp`):

```cpp
struct Person : moira::Object<Person> {
    float height = 6.0f;
    float on_grow(float amount) { return height += amount; }
    static void bind(moira::Binder<Person>& b) {
        b.method("grow", &Person::on_grow);
        b.property("height", &Person::height);
    }
};

Person bob;
ctx.add(bob, "bob");
ctx.eval("{bob grow 10}");                          // script calls C++
ctx.eval("{class Person (miss {set [height] 0})}"); // script handlers for a C++ type
ctx.call(bob, "miss");                              // C++ calls script
```

A failed lookup says what, where, and how it got there:

```
moira: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
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
  fixed arrays built in; see `array.hpp`), opt-in `sort_for_lookup()` binary search, and
  `subarrays()`.
  `find_float` on a 20-child array takes about 10 ns and never allocates.
- `Config`: named layers merged bottom to top (optional and disabled layers, toggles),
  provenance (`source()`: layer, file, line), hot reload with per-group migrate-or-punt
  callbacks, reload telemetry in six timed stages with an injectable time source, live
  edits in a `scratch` layer with an allocation-free fast path, write-back that replaces
  only the changed tokens (or inserts an entry beside its siblings) and refuses, with a
  location, what it cannot edit exactly, `layer_effect`, `diff`, `promote`, and snapshots.
  Patterns are in `docs/cookbook.md`.
- `Tweak<T>`: one-line tunables with code- or data-side metadata (`range`, `step`, `desc`,
  `note`, `kind`, `group`), `on_change`, and `dev_only` for `MOIRA_SHIPPING`.
- Edit-mode parsing (`SourceMap`): the byte span of every node, beside an unchanged tree.
- `Context` (`moira::script`): lazy evaluation with no special forms, native functions
  bound from their signatures (`Raw<T>` for unevaluated parameters, `std::optional` for
  optional ones, `Args` for the rest, overload sets), dynamically scoped `$variables`,
  script functions (`{func}`, with `stub`/`provisional`/`deprecated` status), `eval` of
  text, a generated reference (`docs/script-reference.md`), and `check` with a demand
  report. A native call with two scalar arguments takes about 30 ns and never allocates.
- Objects: `Object<T>` and `Binder<T>` for methods (overloads, docs, status) and
  properties, inheritance flattened at bind time, salted handles that make a reference to
  a destroyed object a clean error, script handlers attached to C++ types (`{class}`),
  script classes and `{new}`/`{delete}`, `$this`, `[property]` paths, and loose
  per-object parameters.
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
| `MOIRA_SHIPPING` | `OFF` | Reload and live edits inert; `dev_only` tweaks keep defaults |
| `MOIRA_NO_EXCEPTIONS` | `OFF` | Non-throwing APIs only |
| `MOIRA_SANITIZE` | empty | e.g. `address,undefined` |
| `MOIRA_BUILD_TOOLS` | `ON` when top-level | `moira_parse_corpus` |
| `MOIRA_BUILD_EXAMPLES` | `ON` when top-level | `examples/` (run by CTest) |
| `MOIRA_BUILD_BENCH` | `OFF` | `moira_bench_parse`, `moira_bench_lookup`, `moira_bench_call` |
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
