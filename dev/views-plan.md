# Plan: W1, views generated from shapes

*Phase W1 of `dev/schema-layer-spec.md` (D269). Terms are `dev/glossary.md`'s. The design
note is §2; §3 is the steps, each committed green.*

**Status: approved 2026-10-31, with Q1 and Q2 answered as recommended; in progress.**

| Decision | Choice |
|---|---|
| What ships | `(@view)`, `(@view no)`, `(@cxx T)`, `(@cxx_name N)`; `view_digest`; `<baker/view.hpp>` with `ListView<V>`, `read_view<T>`, `check_views`; `baker_bake --views`; `baker_generate_views`; `check --baker-strict-views` |
| Where | The grammar, digest and generator in `baker::shape`; the runtime types in `baker::data`; the tool mode in `tools/baker_bake.cpp`; the CMake function in `cmake/BakerViews.cmake` |
| Depends on | Nothing new. W3 and W2 build on it |

## 1. What the data says

The stacks declare 22 shapes (`stacks/*/packs/game/*_core.dta`), and they are the real
values this phase is built around:

- **3 to 7 fields a shape**, so a view is a small struct, and a record has about as many
  children: the linear tag scan is the right lookup, as D77 found.
- **`any` is common:** 20 of the fields are `any`, each a command or nested unshaped data
  (`(goal any)`, `(blocks any)`). A viewed shape marks each `(@view no)`.
- **Lists are short and uncounted:** `(pos (list float))` for x y z, `(recipe (list symbol))`.
  Two or three elements is the common case.
- **Nesting is by keyed wildcard** (`(@at levels * pieces)`), never by a field of shape type.
  Nested views are supported, since `Field::shape` exists, but no stack uses one.
- **Nothing reads through `Convert<T>` today.** The stacks read records one field at a
  time, `a.find_float(Symbol(tag), fallback)` (`stacks/duel/fight.cpp:23-29`), with no
  struct anywhere. So the drift W1 removes is between a shape and a scatter of `find`
  calls, and the "three sources" are the shape, those calls, and whatever struct the game
  copies into.

## 2. Design note

### 2.1 The transforms, by path class

| Transform | Input | Output | Class | How often |
|---|---|---|---|---|
| `baker_bake --views` | the contract layers' `#shape`s | one header a shape, an umbrella, a depfile | offline | per shape change |
| `read_view<T>` | one record's `Array` | a `T` | load | per record at load; a careless game could call it per frame |
| `ListView<V>::operator[]` | a node run | a `V` | hot, if a game iterates one each frame | per element |
| `view_digest` | a `shape::Set` | 64 bits | tool | at load in development, in `check` |
| `check_views` | the running digests against the compiled | warnings | tool | once after load |

W1 adds nothing to a merge, a lookup or a reload. A stack with no `(@view)` pays one
symbol compare per metadata child in `Set::add`, which runs only when the shape validator
is installed, and the validator stays opt-in.

### 2.2 The generated reader walks the record once

A view of F fields read by F `find`s scans the record's tag run F times, each about 9 ns
(D77) on a 20-child record. The generated reader instead walks the record's children once
and dispatches each child by its tag key (`Array::key_of`, a 32-bit compare against the
field's interned key) into the field's slot: N compares and one switch, against F·N/2.
For the real sizes (F ≈ N ≈ 5) either is under 100 ns; the one-pass form is also what a
baked table's builder (W2) runs over every record, so it is the one to write once. Step 3
measures both and records the numbers.

Required fields absent after the walk are one located error naming the first; a value of
the wrong type is a located error naming the field. The reader never allocates: a view's
strings and lists borrow (§2.4).

### 2.3 Two headers a shape: the struct is light, the reader is heavy

A header that reaches `<baker/array.hpp>` costs about half a second a translation unit;
`<baker/fwd.hpp>` costs milliseconds (0.7.0). A game names its views in many files and
reads them in one. So each shape generates:

- `views/boss_phase.hpp`: the struct, `kShapeDigest`, and `kShapeName`. It includes
  `<baker/view.hpp>` only, which reaches `<baker/symbol.hpp>`, `<baker/curve.hpp>`'s
  `Curve` through a forward declaration, and `<string_view>` (`ListView` is defined
  there; `Curve` is a member by value, so `view.hpp` includes `<baker/curve.hpp>` unless
  step 3's measurement says to move curve fields to the reader header).
- `views/boss_phase_read.hpp`: the `ViewReader<BossPhase>` specialization and
  `Convert<BossPhase>`. It includes `<baker/view_read.hpp>`, which reaches `array.hpp`.
- `views/views.hpp`: every struct header, `kViews` (a span of `ViewInfo`), and
  `register_views` for W2.

Step 3 measures the include cost of both headers with the skill's recipe and records it.

### 2.4 `ListView<V>`: a view over a node run, with room for a packed run

```cpp
template <class V>
class ListView {
    const void* data_ = nullptr;  // W1: const Node*; W2 adds packed V
    uint32_t size_ = 0;
    uint32_t packed_ = 0;         // W2: 1 if data_ is V[]
public:
    uint32_t size() const noexcept;
    Expected<V> at(uint32_t i) const noexcept;   // converts the node; an error names the element
    V operator[](uint32_t i) const noexcept;     // the converted node, or V{} if it does not convert
    // iteration over operator[]
};
static_assert(sizeof(ListView<float>) == 16);
```

- W1 ships the node-backed form only. `packed_` is reserved so W2 changes no struct layout
  the game compiled, only the branch inside `operator[]`: one predictable branch an
  element, which the skill's dirty-flag finding says to measure in W2 before keeping.
- A list of nested views is `ListView<Nested>`, whose `at` reads the element array with
  `ViewReader<Nested>`. A list of `key_of` is `ListView<Symbol>`.
- `(list T N)` and `(list T N M)` are the same type; the reader checks the count as
  `shape::check` does, as a located error.
- The alternative, `std::span<const V>` into the tree, is impossible: a node is a 16-byte
  union, not a packed `V`. Copying into a game-owned array is the game's choice, and
  `ListView` iterates for it.

### 2.5 Where the pieces live

- **`baker::data`, `<baker/view.hpp>`:** `ListView<V>`, `ViewInfo {Symbol name; uint64_t
  digest;}`, and the `ViewReader<T>` primary template. **`<baker/view_read.hpp>`:**
  `read_view<T>(const Array&) -> Expected<T>`, `read_view<T>(const Config&, Path) ->
  Expected<T>` (a free function, so `config.hpp` includes nothing new), and the helpers
  the generated readers call (`view_detail::read_int32`, `read_float`, `missing_field`,
  `wrong_type`). Out of line where they build an error.
- **`baker::shape`:** the grammar (`Field::view`, `Field::cxx`, `Field::cxx_name`,
  `Shape::view`, `Shape::cxx_name`), `view_digest(const Set&, Symbol)`,
  `contract_set(...)` (the Set of the contract layers, with the `#ifdef` rules),
  `write_view_header`, `write_view_reader`, `write_views_umbrella`, and `check_views`
  (it needs a `Set`, so it is shape's).
- **`tools/baker_bake.cpp`:** `--views OUT_DIR --project FILE [--depfile FILE]`. The tool
  stays thin: it reads the project, builds the contract set, and calls the writers.
- **`cmake/BakerViews.cmake`:** `baker_generate_views(target PROJECT file [NAMESPACE ns])`.
- **`baker::host`:** `CommandHooks::views` and `check --baker-strict-views`.

### 2.6 Shipping

- A view struct carries `kShapeName`, and `kViews` carries each name: tool text. Under
  `BAKER_SHIPPING` both are empty (`baker::detail::tool_text("boss_phase")`, which D256's
  tweaks already use or step 3 adds), and `check_views` names a view by its digest.
- `read_view` is the same in every build; `check_views` compiles to nothing under
  shipping, as `each_reload` does.

### 2.7 The digest

`view_digest` folds the canonical text of §W1 of the spec with `symbol_hash`'s FNV-1a 64
(D246), one call, so the generator and the runtime share one function and one promise.
Golden digests are `static_assert`ed in the tests, as D246's hashes are, so a change to
the canonical text is a deliberate one.

### 2.8 The `#ifdef` rules

- A `ShapeDecl` whose line lies inside a `ConditionalSection` of the same file is a
  generation error naming the shape: the session records both today.
- A `#shape` directive inside a skipped branch is not read today. The lexer, which scans
  skipped lines for their `#else` and `#endif`, records a `#shape` it passes as
  `{file, line}` in a new `LoadSession::skipped_shapes()`, and generation names it. The
  cost is one compare per skipped directive line, on the load path, only inside skipped
  branches.
- A `(when COND)` or `dev_only` contract layer is a generation error at the project file.

### 2.9 What this plan changes in the spec

- `read_view<T>(const Array&)` names the field's file and line and the field; the path is
  known only to `read_view<T>(const Config&, path)`, which adds it.
- `Config::view<T>(path)` is the free function `read_view<T>(const Config&, Path)`.
- Two headers a shape, not one (§2.3).

## 3. Steps

Each step runs `tools/local_matrix.sh` and clang-tidy before its commit, and records its
result in its D-entry (D273 onward).

1. **Grammar.** `(@view)` and `(@cxx_name N)` on a shape; `(@view no)`, `(@cxx T)` and
   `(@cxx_name N)` on a field; the closed `@cxx` list; `Field` and `Shape` members;
   `baker.shapes/1.6` writes them; `docs/language.md`. Tests: each form parses, each error
   is located, a stack's shapes are unchanged by the parser.
2. **The digest and the contract set.** `view_digest`, with golden `static_assert`s;
   `contract_set(session, layers, report)` with the `#ifdef` rules and
   `skipped_shapes()`. Tests: the digest ignores every tuning word and moves on a type,
   a name, an order, a `@cxx`, and a `(@view no)`; each `#ifdef` case errors.
3. **The runtime types.** `<baker/view.hpp>`, `<baker/view_read.hpp>`, a hand-written
   `ViewReader<BossPhase>` of exactly what step 4 will generate, and the behaviour tests:
   wrong type, missing required, optional defaults, overflow, nested view, lists and
   counts, borrowed strings alive across a reload while the `Ref` is held. Measure: the
   one-pass reader against F finds (`bench_lookup`), and both headers' include cost. The
   allocation test covers a read.
4. **The generator.** `write_view_header`, `write_view_reader`, `write_views_umbrella`,
   deterministic; naming rules and every generation error. Tests: golden headers for
   `tests/shape/views/*.dta`, and a compiled test that includes the generated headers and
   reads records from `tests/shape/bosses.dta`.
5. **The project file and the tool.** `(views (layers ...) (shapes ...) (namespace ...))`;
   `baker_bake --views` with the depfile and write-if-changed; `baker_bake` built whenever
   `baker::shape` is (§4, Q1); `baker_generate_views` with `BAKER_BAKE_EXECUTABLE`; the
   stacks' build runs it for one stack. Tests: a CTest that generates from a project and
   builds a TU against it; a second run writes nothing.
6. **Drift.** `CommandHooks::views`, `check` warns, `--baker-strict-views` errors,
   `baker.check` carries the mismatch; `check_views` in `examples/`; `docs/tools.md`,
   `docs/check-json.md`.
7. **The example, the docs and the record.** `examples/content-tables` (W1 part),
   `docs/cookbook.md`, the changelog lines in the D-entry, the no-cost rows (`bench_layers`
   with `BAKER_BENCH_SHAPE`, `bench_cache`) before and after, and the skill's
   `baker-gaps.md` row.

## 4. Open questions for the owner

Asked with AskUserQuestion before step 1; their answers go in D273.

- **Q1. `baker_bake`'s build gate.** It is built only under `BAKER_BUILD_TOOLS`, which the
  stacks force OFF and which defaults OFF when Baker is not the top-level project. A
  consumer's `baker_generate_views` needs it. Recommended: build `baker_bake` whenever
  `BAKER_BUILD_SHAPE` is on, as a target of its own (it links `baker::data` and
  `baker::shape` and is a few hundred lines), and keep `BAKER_BUILD_TOOLS` for the rest.
- **Q2. The struct header's `Curve`.** `Curve` by value in a struct header pulls in
  `<baker/curve.hpp>`. Recommended: include it, and let step 3's measurement decide if a
  curve field should instead be read through the reader header.
