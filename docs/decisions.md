# Decisions

Deviations from and clarifications of `docs/spec.md`, with the reasoning. Short entries,
newest last. Background documents live in `docs/background/`.

---

## D1. `#merge` fills missing keys; Config layers override

*Spec §4, Appendix A.*

The spec calls the slide's merge example — `(a (b 1))` merged with `(a (b 2) (c 2))`
giving `(a (b 1) (c 2))` — a typo, and asks for last-write-wins. Re-reading Appendix A,
the slide is consistent with how the original was used: the *app-level* file `#merge`s
`default.dta`, so the app file is loaded first and defaults are merged *into* it. With
that direction, first-write-wins is exactly what lets the app file override. Community
format notes describe `#merge` the same way.

So we have both, explicitly:

- **`#merge` directive in a data file:** fill-missing. Existing keys keep their value,
  missing keys are inserted, subarrays present on both sides recurse. Faithful to DTA, so
  files written for the original behave the same.
- **`Config` layers (`push_layer`, `load`/`merge` shorthands):** last-write-wins. Layers
  merge bottom to top and higher layers override leaves.

One merge routine with a policy flag, not two implementations. The source carries a
comment pointing here so the next person who checks the slide knows why.

## D2. The line budget is a soft target

*Spec §0, M10.*

*Raised to 8,000 by D40.*

4,000 non-test, non-comment lines is tracked and reported (`tools/count_lines.py`,
printed in CI per target), not enforced. The full scope — edit-mode write-back, layers,
telemetry, `check` and its reports, coroutine tasks, binary cache — is expected to exceed
it. Smallness still matters: every milestone records its line count here, and growth that
looks disproportionate gets called out rather than absorbed.

## D3. No Harmonix files in the repository or CI

*Spec §8, M1.*

Genuine Harmonix `.dta` files are Harmonix copyright, which conflicts with keeping the
repository cleanly MIT. We vendor none and CI fetches none.

- CI's corpus test runs over **original fixtures** in `tests/corpus/`, hand-written to
  exercise the same syntax real files use (quoted symbols, macros, `#ifdef`, `#merge`,
  commands, properties, odd comments, hex ints, etc.).
- `tools/parse_corpus` (M1) points the parser at any local checkout, e.g. of
  `mariteaux/milo-script-library`, for a manual 100% parse check. The M1 "real corpus at
  100%" gate is satisfied by running it locally and recording the result here.
- The fuzzer is seeded from the original fixtures.

## D4. `Object` nodes hold a salted handle, never a pointer

*Spec §1 table vs. §5; `background/halo-tags-vs-dataarray.md` §6.*

The §1 table says `Object*`; §5 says a 32-bit salted handle. The handle wins. In
`moira::data`, `Type::Object` is an opaque 32-bit handle (16 index + 16 salt) that copies,
compares, prints, and round-trips but cannot be resolved; `moira::data` knows nothing
about `Context`. `moira::script` owns the per-`Context` object table and does strong
(`get`) and weak (`try_get`) resolution.

## D5. Handler cache: rewrite slot 0 in place

*Spec §5, "Handler caching"; §2 layout targets.*

The array header has no room for a cached handler plus generation. As in the original,
first execution rewrites the command's slot 0 from `Symbol` to `Func`, pointing at the
handler record (which keeps the name, so printing, write-back, and error messages are
unchanged). The registry generation is stored in the node's 3 spare padding bytes (24
bits); a mismatch re-resolves. Node and header sizes are unchanged.

Consequence to keep in mind: execution mutates the array. That conflicts with
"multi-thread-read-after-freeze" for *script* arrays executed concurrently, and with
macros sharing arrays across sites (harmless: the rewrite is idempotent and
name-preserving). If either bites, or the M4 benchmark shows the cache doesn't pay, drop
it and record that here.

## D6. Tags are Symbols only

*Spec §3.*

`find` matches only arrays whose first node is a `Symbol`. Lookup stays a pointer
comparison, and "same tag" means the same thing everywhere: lookup, merge, sorted mode,
provenance, write-back, and shapes. Accepting String tags too would make merges silently
duplicate entries whenever a key was spelled one way in the base and the other in an
override. Real data overwhelmingly keys by symbol; string-first arrays are mostly ordered
display lists and plain string lists.

Keys that aren't bare symbols use the quoted form, `'bacon and eggs'`. The spec's menu
example is corrected accordingly. To catch the mistake this invites, a *failed* lookup
checks for a String-tagged child with the same text and suggests the quoted-symbol
spelling in the error. That check is on the error path only.

## D7. Background documents

The spec's `reading-a-studio.md` was supplied as `harness-lens-on-development.md`; it is
stored under the spec's name. All six companion documents are in `docs/background/`.

## D8. Known extension point: epoch-cached pure commands

*From `background/seexpr-vs-moira.md` §8.* A command whose inputs are all tweaks is
constant until reload and could be memoized on the reload epoch if its functions were
marked pure. Not built — it adds a purity annotation and an evaluation cache for a
workload Moira doesn't have. Recorded so it isn't reinvented by accident.

## D9. M0 build layout

- Three CMake targets: `moira_data` (`moira::data`), `moira_script` (`moira::script`,
  links `moira::data` PUBLIC), and the interface umbrella `moira` (`moira::moira`).
  `MOIRA_BUILD_SCRIPT=OFF` builds the data layer alone; CI has a job that does exactly
  that, so a reverse dependency fails the build.
- Build switches: `MOIRA_BUILD_SCRIPT`, `MOIRA_BUILD_TESTS`, `MOIRA_ATOMIC_REFCOUNT`,
  `MOIRA_SHIPPING`, `MOIRA_NO_EXCEPTIONS`, `MOIRA_SANITIZE` (`address,undefined` etc.).
  Feature switches become compile definitions on the library targets.
- doctest 2.4.11 vendored under `third_party/doctest/` (MIT), recorded in
  `THIRD_PARTY.md`.

## D10. Lexical details the spec leaves open

*Spec §1. M1.*

- **Whitespace** is every byte <= 0x20, so stray control characters never break a file.
  A UTF-8 byte order mark at the start of a file is skipped.
- **Comments:** `;` to end of line, and `/* ... */`, which starts only at the beginning of
  a token (so `a/*b` is one symbol). An unterminated block comment is an error.
- **Strings** have exactly two escapes, `\n` and `\q` (double quote), as in DTA. Every
  other backslash is literal, so Windows paths need no doubling. Strings may span lines.
- **Quoted symbols** `'like this'` have no escapes and may span lines. They are never
  macro-expanded, which makes them the way to write a symbol that collides with a macro.
- *Numeric ranges below were widened by D18.*
- **Integers:** decimal must fit int32, or it is an error. `0x` hex may use all 32 bits
  and keeps its bit pattern (`0xFFFFFFFF` is -1), because hex is how flags and colors are
  written. A leading `+` or `-` is allowed on both.
- **Floats** must fit a float (`1e39` is an error). `inf` and `nan` are symbols.
- An atom that is not a number (`1e`, `0xZZ`, `1.2.3`, `12abc`) is a symbol, not an error.

## D11. Directive details

*Spec §1. M1.*

- Paths after `#include` / `#merge` may be bare or `"quoted"`; real files use bare paths.
  They resolve against the including file's directory, then `LoadSession::include_dirs`.
- Two directives beyond the spec, both from the DTA format: `#include_opt` (missing file is
  not an error) and `#autorun {cmd}`, whose commands are collected in
  `LoadSession::autorun` for the host or `moira::script` to run.
- `#define NAME (body)` defines a macro, but only if the `(` is on the same line as the
  name; otherwise `#define NAME` is a flag and the array that follows is ordinary data.
  Macros and flags share one table, so `#ifdef` sees both.
- `[NAME ...]` defines a macro only at a file's own top level; deeper, `[...]` is a
  Property. Consequently a Property can never sit at the top level of a parsed tree (it
  would print back as a macro definition); splicing one there is an error.
- Conditionals are token-level and may span brackets; they must close in the file that
  opened them. Unknown directives are errors, except inside a skipped branch.
- Include cycles, include depth (32), included-file count (4096), nesting depth (256,
  counting arrays spliced from macros), and total nodes per read (2^26) are all bounded by
  `ParseLimits`, so an exponential macro or a self-include fails cleanly.

## D12. Array header layout

*Spec §2.* The 24 bytes are: refcount 4, size 4, file Symbol 8, line 4, height 2, flags
2. `height` (1 + tallest child array) exists so the parser can bound nesting even through
shared macro arrays, which in turn bounds every recursive walk (destruction, printing,
equality, merge). `flags` is reserved for M2's sorted-lookup mode.

## D13. Values the printer cannot spell

*Spec §1, round-trip.* A parsed tree always round-trips (fuzzed). Trees built in C++ can
hold values DTA text has no spelling for; the printer does its best and documents them:

- a symbol that needs quoting and also contains `'`;
- a string containing a literal backslash followed by `n` or `q`;
- NaN or infinite floats (printed as `nan` / `inf`, which read back as symbols);
- Object handles (printed as a quoted symbol `'<object 3:17>'`);
- a Property node at a root's top level (D11).

The printer also never starts a file with UTF-8 BOM bytes (it adds a leading space): the
reader skips a BOM there. The fuzzer found that case.

## D14. Type errors from a bare Node have no location yet

*Spec §2.* A Node does not know which array holds it, so `Node::as_int()` on a leaf
reports the value and types but no file or line. (A mismatched *array* node reports its
own location.) M2's array-level accessors (`find_int` and friends) add file, line, and
array path. This is also how the float/int rule reads: an int always reads as a float; a
float reads as an int only when integral and in range, so `12.75` as an int is an error
while `12.0` is 12. That reconciles §2's "coerce freely" with its own error example.

## D15. `#merge` details

*D1, spec §4.* Fill-missing merge matches arrays by symbol tag only (D6). Non-array nodes
in the merged file are ignored; arrays whose first node is not a symbol are appended. Merged
arrays are rebuilt, never mutated in place, because macro expansion may share them. The
overwrite policy for Config layers arrives in M3.

## D16. Symbol table locking

*Spec §2.* Interning takes a mutex, so files may be parsed on several threads. Parse
throughput is about 115 MB/s on the generated benchmark (Release, gcc 13, one thread),
well above the 40 MB/s target, so there is no reason yet for a lock-free table.

## D17. M1 results

- **Corpus.** Per D3 there is no Harmonix corpus here. The hand-written corpus in
  `tests/corpus/` parses and round-trips at 100%, and so does every prefix of every
  file. The real-corpus gate is run locally with
  `moira_parse_corpus --roundtrip <checkout>`; first run 2026-10-13, recorded in D101.
- **Fuzzing.** `moira_parse_fuzzer` (libFuzzer, ASan+UBSan, seeded from the corpus) checks
  that every input either fails with a located error or parses and round-trips. It found
  two bugs, both in BOM handling (`tests/fuzz_regressions/`), and then ran clean for 1.2M
  execs (the M1 gate). A 10M-exec run (spec §8) then finished clean: 10,000,000 execs in
  17 minutes, 940 edges covered, a 950-input corpus grown from the seeds, no failures.
  CI fuzzes for two minutes per push.
- **No-exceptions build** compiles with `-fno-exceptions` and runs the full suite; CI has
  a job for it.
- **Size.** `moira::data` is about 1,920 counted lines after M1, roughly half of the
  4,000 soft budget (D2). The parser and preprocessor are about 640 of those, `Node` about
  260. As D2 predicted, the full scope will pass 4,000; candidates for trimming later are
  the `expected` shim (about 100 lines, gone once C++23 is the floor) and the error-message
  strings in the parser.

## D18. Numbers are stored at 64 bits

*Deviates from spec §1 ("Int/Float are deliberately 32-bit ... do not widen the storage").
Decided 2026-10-11 after the core-types research (`background/core-types-references.md`).*

Int is stored as `int64_t` and Float as `double`. The spec's reason for 32 bits was node
size, but on the 64-bit targets Moira requires the payload is 8 bytes anyway because it
holds pointers, so widening costs nothing; `sizeof(Node)` is still 16. Every reference
value type examined stores scalars at 64 bits (Lua 5.4, Luau, Godot's Variant, UE5's
doubles for world positions), and 32-bit floats represent integers exactly only to
±2^24 and positions to roughly ±32 km.

- **Reads:** `as_int()` returns `int64_t` and `as_float()` returns `double`. `as<T>()`
  reads any arithmetic type, with a range check for narrower types (`as<int32_t>()`,
  `as<uint8_t>()`, `as<float>()`). An int reads as a float (rounded beyond 2^53); a float
  reads as an int only if integral and in range (D14 otherwise unchanged).
- **Construction:** any integer or floating-point type converts to a Node, so `long`,
  `int64_t` and `unsigned` no longer collide.
- **Literals:** decimal integers must fit int64. A hex literal denotes its unsigned
  value, so `0xFFFFFFFF` is 4294967295 (no longer -1), and hex above
  `0x7FFFFFFFFFFFFFFF` is an out-of-range error rather than a silent negative. Floats
  must be finite doubles; literals that overflow or underflow to zero (`1e309`,
  `1e-400`) are errors, subnormals are fine.
- **Parsing** uses vendored fast_float everywhere, not `std::from_chars`. libc++ only
  gained double `from_chars` in LLVM 20, and Apple only ships it for iOS/macOS 26
  deployment targets (verified against libc++'s `availability.h`), so the previous code did
  not compile for Android NDK r28 and earlier or for older Apple targets. Using one parser
  everywhere also makes edge cases (subnormals, out-of-range) identical on every platform.
- **Printing** uses `std::to_chars` (shortest round-trip) except on Apple, where libc++
  gates double `to_chars` behind newer deployment targets; there a portable formatter
  picks the shortest `%g` that reads back exactly and normalizes the locale's decimal
  point. Both are tested to round-trip 20,000 random doubles. CI gains a libc++ job.

## D19. Object handles are 64-bit

*Deviates from spec §5 (16-bit index + 16-bit salt). Refines D4.*

`ObjectHandle` is a 32-bit table index plus a 32-bit salt, the layout of Unreal's
`FWeakObjectPtr` (`ObjectIndex` + `ObjectSerialNumber`). 65,536 live objects per context
is too few for current games, and EnTT's and flecs' defaults are far larger. The handle
still fits the Node's 8-byte payload, so this costs nothing; moira::script (M5) is its
first user.

## D20. Refcounts are atomic by default

*Deviates from spec §2 (non-atomic by default, atomic as an option).*

`MOIRA_ATOMIC_REFCOUNT` now defaults to ON. Copying a Node that holds a String or Array
writes its refcount, so the spec's "multi-thread read after freeze" model was only safe if
readers never copied a Node, an easy rule to break. Unreal made the same call: UE5's
`TSharedPtr` defaults to `ESPMode::ThreadSafe`. The evidence offered for non-atomic counts
in job-system engines turned out to be unverifiable (`background/core-types-references.md`).

Cost: no measurable difference on the parse benchmark (about 145–170 MB/s either way,
within run-to-run noise). M2's lookup benchmarks will measure copy-heavy paths. The
setting is always passed as a PUBLIC compile definition, because translation units that
disagree would violate the ODR; `-DMOIRA_ATOMIC_REFCOUNT=OFF` remains for
single-threaded programs and has its own CI job.

## D21. The Expected<T> choice is made once, by the library's build

`Expected<T>` is `std::expected` in C++23 and Moira's shim in C++20. Compiled functions
like `try_read_file` return it, and the Itanium ABI does not mangle return types, so a
C++20 library linked into a C++23 program (or the reverse) would have linked silently
with two different layouts. CMake now checks once, at configure time, whether the
library's build has `std::expected`, and exports `MOIRA_STD_EXPECTED=0/1` with the
target. A consumer that cannot match gets a compile-time `#error`; a C++23 consumer of a
C++20 build simply uses the shim too. Builds without CMake fall back to detecting it per
translation unit, and must then compile everything at one standard.

## D22. Portability fixes from the core-types review

- **No `std::filesystem` in the library.** Moira never opens files itself (the
  `LoadSession` reader does), so include paths are joined and normalized lexically, in
  about 40 lines; both `/` and `\` separate. This drops a dependency that console and VFS
  users avoid. The tools and tests still use it.
- **MSVC no-exceptions** builds now actually disable exceptions (`/EHs-c-`,
  `_HAS_EXCEPTIONS=0`) and have a CI job. Exceptions-off is the norm in shipping game
  builds (EASTL defaults to it; see the references).
- **Android:** clang defaults to C++17, so `ndk-build` users must add `-std=c++20`
  themselves; CMake users get it from the target. Noted in the README.

## D23. Symbols stay case-sensitive

*Decided 2026-10-11.* Unreal's `FName` compares case-insensitively and keeps the first
spelling for display. Moira keeps DTA's case-sensitive identity instead: "first spelling
wins" would let the printer change a symbol's case depending on load order, which breaks
round-tripping and minimal write-back diffs (M3). To catch `Health` versus `health`
mistakes anyway, a failed lookup suggests a case variant of the name when one exists (M2,
alongside D6's quoted-symbol hint), and `check` warns when data mixes case variants of a
name (M4).

## D24. Results after D18–D23

Re-verified on 2026-10-11 with every configuration passing locally:
- gcc 13 and clang 18 at C++20 and C++23;
- clang 18 with **libc++ 18**, which is what failed before D18;
- data-only, no-exceptions, plain refcounts, ASan+UBSan, TSan, and shipping builds;
- a build with the Apple formatting fallback forced on.

The fuzzer ran 2M execs clean on the new number handling, reaching 1,325 edges.
`moira::data` is now 2,063 counted lines; vendored fast_float is third-party and not
counted (D2).

## D25. Lookup API shape

*Spec §3, M2.*

- **`find(tag)` returns `const Array&`**, not a pointer, so chains read
  `menu.find("eggs").find_float("price")` rather than the spec's `->`. The talk's
  `FindArray` returned a pointer because it had no exceptions; ours throws on a miss, so a
  pointer that is never null would only invite needless checks.
- **`try_find(tag)` returns a nullable `const Array*`**, the spec's "optional-ish" form,
  rather than `Expected`: it is the zero-cost "is it there?" question and allocates
  nothing. Every *typed* lookup (`find_int`, `find<T>`, `get<T>`, ...) has a `try_` twin
  returning `Expected<T>` with a located error, as the spec asks. `try_get(path)` follows
  `try_find`.
- **`find_int` returns `int64_t` and `find_float` returns `double`**, as `as_int` and
  `as_float` do since D18. `find<int32_t>`, `find<float>` and the like narrow with a range
  check.
- **Fallback overloads return the fallback only when the key is absent.** A present value
  of the wrong type is still an error: silently using the default because a designer wrote
  `(price "12")` is the failure located errors exist to prevent. Because they can throw,
  the fallback forms are compiled out under `MOIRA_NO_EXCEPTIONS`; the twin is
  `try_find(tag, fallback)` / `try_get(path, fallback)`, which returns `Expected<T>` with
  the same semantics. (`try_find<T>(tag).value_or(x)` is the "default on any failure"
  form, if that is really wanted.)
- **`find_str` reads String nodes only**, like `as_str`. Symbols and strings stay distinct
  types everywhere (D6).
- **Error paths.** A lookup along a `Path` reports the full path walked, matching the
  spec's example exactly. A chain of `find()` calls reports only the tag of the array the
  failing call was made on: arrays have no parent links (they are shared by macros, and the
  header has no room), and a returned handle carrying its ancestry would dangle as soon as
  someone stored it. The file and line, which locate the value precisely, are always
  present.
- **Commands and Vars.** Typed lookups are where M4's implicit evaluation will happen. In
  M2 a Command or Var value is a clean error saying it needs `moira::script`, which is also
  the spec's required behavior for a data-only build. Raw access (`find`, `at`,
  `Convert<Node>`) never evaluates.

## D26. `Convert<T>` is a class template, and reads one way for now

*Spec §5 ("an ADL-findable customization point").*

`Convert<T>` is specialized, like `std::hash` or `fmt::formatter`, rather than found by
ADL. A converter for a type you do not own (`glm::vec3`) then needs no names added to
someone else's namespace. The interface is `static bool from(const Values&, T&)` plus an
optional `name` for error messages; returning `bool` keeps converters short and
exception-free, and the library builds the located error itself. The reverse direction
(`T` to `Node`) arrives with M3/M4, which are the first to need it.

`Values` is the run of nodes a lookup found: the values after a tag. Scalars read the
first value and ignore the rest, which the inline-metadata syntax requires:
`(width 2 (range 0 10) (step 1))` must read as 2. `Values::read(i, out)` reads one element;
for compound types an element that is an array reads as its contents, so one `Convert<Vec2>`
handles `(size 1920 1080)`, `(points (1 2) (3 4))` as `std::vector<Vec2>`, and
`renderer.points.1`. Scalars never descend, so an array element is never silently read as
its first number. Built in: all arithmetic types (range-checked), `bool`, `Symbol`,
`std::string_view`, `std::string`, `Node`, `std::vector<T>`, `std::array<T, N>`.

## D27. Path syntax

*Spec §4 (`cfg.get<float>("renderer.clear_color.0")`).*

`Path` is shared by `Array::get` and M3's `Config::get`. Segments are separated by `.` or
`/` (errors print paths with `/`, as in the spec's example, so a path copied out of an
error message works); `'quoted'` segments hold any character but `'`; an all-digit
segment is an index. A name selects the child array with that tag and continues with its
values; an index selects the n-th value (0 is the first value after the tag, so
`clear_color.0` is the red channel), continuing inside it if it is an array. A name applied
to a leaf finds nothing (absent, not a type error). Malformed text (`a..b`, a trailing
separator, an unterminated quote) makes an invalid `Path`, and looking one up is a Syntax
error even with a fallback.

Parsing interns every name, about 150 ns for a four-segment path. Hold a `Path` in a
static or member for hot lookups (18 ns for the same four segments).

## D28. Sorted mode reorders the array

*Spec §3 ("`sort_for_lookup()` marks an array as sorted by tag").*

The header has two spare bytes, not room for a side index, so sorting reorders the
children in place: everything that is not a tagged array moves to the front in its
original order (so a tag in slot 0 stays in slot 0), and tagged arrays follow, stably
sorted by symbol hash and then text. That order depends only on the text, so printing a
sorted array is deterministic across runs, and the stable sort keeps duplicate tags in
order, so `find` still returns the first. `find` then does a `partition_point` (no extra
storage). `set()` clears the flag; re-tagging a child in place is not detected and is
documented. Measured crossover is about 50 children: at 20, linear is faster (spec §3's
"linear is the right default" holds); at 200, sorted is 3x faster; at 1000, 6x.

## D29. Empty arrays carry one placeholder node

*Spec §2 ("one allocation per array, ... sized exactly").*

`find`'s scan reads each child array's first node. To skip the per-child size check, an
empty array's allocation holds one `Int 0` node past its (zero) size, which is never
visible and never destroyed. That turns the scan into two branches per child instead of
six and took the worst case of `find_float` on 20 children from 27 ns to 16 ns (gcc 13).
The cost is 16 bytes per empty array, which are rare in real data. The M8 binary cache
must keep the invariant.

## D30. M2 results

*Recorded 2026-10-11, Release, gcc 13 / clang 18, one thread, this container.*

| Benchmark (20-child array, interned keys) | gcc 13 | clang 18 | Target |
|---|---|---|---|
| `find_float`, average over the 20 keys | 9.6 ns | 7.2 ns | ≤ 25 ns |
| `find_float`, 20th key (worst case) | 16.5 ns | 11.0 ns | ≤ 25 ns |
| `try_find_float`, worst case | 15.8 ns | 11.5 ns | |
| `find_float("key19")`, interning every call | 31 ns | 26 ns | |
| `get<int>(Path)`, 4 segments, held `Path` | 18.8 ns | 18.2 ns | |
| `get<int>("...")`, parsed every call | 149 ns | 147 ns | |

Spec §7's target is met with margin. Calling with a string literal interns on every call
(a hash and a mutex), which roughly doubles the cost; a hot loop should hold a `Symbol`,
and M3's `Tweak<T>` makes the question moot for config. Successful lookups make no
allocations, enforced by a test with a counting `operator new` (`tests/support/`).
Parse throughput is unchanged at about 127 MB/s.

- **Errors** carry the key, expected and actual types, file, line, and path; failed name
  lookups suggest the quoted-symbol spelling (D6) or a case variant (D23).
- **Fuzzing.** A second libFuzzer target, `moira_lookup_fuzzer`, takes a path and DTA
  text and checks that every typed lookup either succeeds or fails with a located error,
  that a path prints back as itself, and that sorted and linear `find` agree on every array
  in the tree. It ran 2.2M execs clean under ASan+UBSan locally; CI runs it for a minute
  per push, and a unit test runs the same properties over the corpus.
- **Builds.** Passing locally with warnings as errors: gcc 13 and clang 18 at C++20 and
  C++23, clang with libc++, data-only, ASan+UBSan, TSan, plain refcounts, shipping, and
  no-exceptions (gcc and clang). The parse fuzzer re-ran 1.36M execs clean after the
  empty-array change. MSVC is covered only by CI. Under TSan the allocation-count test
  reports itself skipped, because TSan's runtime owns `operator new`.
- **Size.** `moira::data` is 2,718 counted lines, up 387 from M1. Lookup, paths, sorted
  mode, and their error messages are about 250 of those; the built-in converters most of
  the rest.

## D31. How Config layers merge

*Spec §4, D1, M3.*

Within a tagged array, the *values* are every node after the tag that is not itself a
tagged array (numbers, strings, commands, untagged arrays like `(1 2)`), and the
*children* are the tagged arrays. Overwrite merging: if the incoming array has any values,
they replace the existing values as a list; children merge by tag, recursively; missing
children are appended. So `(width 3)` over `(width 2 (range 0 10))` gives
`(width 3 (range 0 10))`, and `(clear_color 1 0)` over `(clear_color 0.1 0.1 0.12)` gives
`(clear_color 1 0)`, not a positional mix. Duplicate tags match the first occurrence, as
`find` does. Repeated tags therefore behave as a list only within one layer, so an override
that must reach the third `phase` needs keyed phases (`phase_3`).

`#merge` (fill-missing, D15) and layers (overwrite) are one routine, `detail::merge_into`,
with a policy flag; `moira::merge()` exposes it. Merging never mutates: changed arrays are
rebuilt, unchanged ones are shared, and a rebuilt array takes the file and line of whichever
side supplied its values.

## D32. Provenance comes from the layer trees

*Spec §4 ("Provenance").*

Arrays have no room to record a layer, and merging rebuilds them. Instead, `source(path)`
asks the layers themselves, top down: the value comes from the topmost layer whose own tree
has values at that path, which is exactly the overwrite rule above. That layer's array gives
the file and line, even when the value was pulled in by `#include` or `#merge` within the
layer. A path that names a pure container reports the topmost layer that has it. Live edits
report layer `"live"`. Anonymous layers from `load()`/`merge()` are named by their path.

## D33. Edit mode is a side table of spans

*Spec §1 ("Edit-mode parsing and write-back").*

`LoadSession::source_map` turns a parse into an edit-mode parse. The `SourceMap` keeps each
file's text and, per array, the byte span of the array and of every node in it, plus the
macro name when a node was spliced in from a macro. The spec's "trivia" (comments and
whitespace) is not stored separately: it is the text between spans, and write-back
splices tokens into the original text, so it is kept by construction. Nodes and arrays are
unchanged; a gameplay parse records nothing and pays one null check per node. Spans are
carried through `#include` and through `#merge`'s rebuilt arrays. The map holds a `Ref` to
every array it describes, so an address cannot be reused while it is a key. The parse
fuzzer now also checks that every span lies in the text and every literal scalar token
re-reads as its node.

## D34. Write-back

*Spec §1, §4.*

- **Live edits.** `cfg.set(path, value)` and `Tweak::set` put the value in a `"live"` layer
  above all others, so it shows in `source()` and `unsaved()` until it is saved. Without a
  Config, `Tweak::set` just changes the value.
- **`write_back(path)`** finds the layer that supplies the value beneath the live layer
  (D32), walks that layer's tree, and replaces only the value tokens. Each one must be a
  literal in a file. Unchanged values keep their original text, so `3` is not rewritten as
  `3.0`. If the number of values changes, the old ones must be adjacent. Floats are written
  as their shortest decimal (`0.7f` as `0.7`).
- **Refusals** are `ErrorCode::Refused` with the file and line of what would have had to
  change: a value spliced from a macro (naming the macro), an array assembled by `#merge`
  without spans, a layer with no file (an overlay), a layer not read in edit mode, or a file
  whose text on disk differs from what was read ("reload before writing back"). It never
  guesses.
- **`write_back_to(path, layer)`** is the explicit "different layer" call. If the key is
  missing in that layer's file it is added, as `(width 4)` inside the deepest existing
  ancestor, or as a new top-level form at the end of the file.
- After writing, the layer and those above it are re-read, and the live edit is dropped
  only if the files now produce the same value. Writing below a layer that overrides the
  value keeps the live edit, so the running value does not jump.
- The library writes through a replaceable `Writer` (default: `fopen`), as it reads through
  the session's `Reader`.

## D35. Reload compares content, and re-reads from the lowest change up

*Spec §4 ("Hot reload").*

Each file layer records every file its parse read *or looked for* (include candidates
included), with a hash of the content. `reload()` re-reads them through the session's
reader; any difference, appearance, or disappearance marks the layer changed. No clocks or
file-system metadata are involved, so it works the same over a socket-backed reader. The
lowest changed layer and every layer above it are re-parsed, because layers see the macros
of the layers below. That is per-layer, not strictly per-file, but correct. A reload is all
or nothing: a parse error returns a located error and the previous trees stay. `remove_layer`
re-reads the layers above for the same reason, so a removed layer's macros leave no trace.

## D36. Reload callbacks are per group

*Spec §4 ("Migrate or punt").*

`on_reload(group, rebuild, migrate = {})` fires only when the subtree at `group` changed
(compared structurally), which makes the spec's convention ("a group is the unit that gets
refreshed and notified together") operational. Without `migrate` the outcome is a punt and
`rebuild` runs: no code beyond the rebuild itself. A `migrate` returning `Reload::Migrated`
skips the rebuild. Layer pushes, pops and removals, reloads, and live edits all fire the same
callbacks, after tweaks have been rebound and their `on_change` callbacks run.

## D37. Tweak details

*Spec §4 (`Tweak<T>`).*

- The registry is an intrusive doubly linked list behind a spinlock (`std::atomic_flag`),
  and the global one is `constinit`, so registration works from any static initializer,
  in any order, without allocating (tested with the counting `operator new`). The parsed
  path and the `on_change` list are created on first use, because MSVC's debug library
  allocates even for an empty `std::vector` or `std::string`; CI's MSVC job caught that.
- A `Config` attaches to `ConfigOptions::tweaks` (the global registry by default) and
  rebinds every tweak on each change; a tweak registered later binds at once.
- `T` is stored in a `std::atomic<T>` only when that is always lock-free (`float`, `int`,
  small PODs). Anything else (`std::string`, `std::array<float, 3>`) is read with `get()`,
  valid until the next reload, under the single-threaded-mutation rule. A codegen test
  compiles a `Tweak<float>` read at `-O2` with GCC and Clang and checks the assembly: one
  load and no call, plus a GOT load under PIC.
- Declare tweaks at namespace scope. A function-local static adds a guard check per read.
- A value that does not convert keeps the default and is reported through
  `set_warning_handler` with a located error. An absent path is never reported: unbound is
  normal.
- Metadata from data wins (`range`, `step`, `desc`, `kind`, `group`), `note` is data-only,
  `kind` is inferred as `color` for 3 or 4 numbers ranged 0..1, and `group` defaults to the
  path without its last segment.
- Every `TweakMeta` member has its own initializer: clang 18's
  `-Wmissing-field-initializers` otherwise warns on the spec's own one-liner
  `{.range = {0, 10}, .step = 1, .desc = "Box width"}`.

## D38. freeze() marks the end of the initial load

*Spec §6 (`MOIRA_SHIPPING`: "reload and layer mutation no-ops after initial load").*

"After initial load" needs a moment to happen at, so `Config::freeze()` is it, in every
build: afterwards reload, layer changes, and live edits are refused. It is useful outside
shipping too, for anything that wants the config immutable. Under `MOIRA_SHIPPING`,
`reload()` is always inert, live edits and write-back are refused, and `dev_only` tweaks
ignore the data. `ConfigOptions::live_layers` is the spec's "explicitly enabled layer path":
it lets layers change after `freeze()`, for live-ops overlays in a shipped game.

## D39. M3 results

*Recorded 2026-10-11.*

- **Done-when criteria**, each a test: `examples/one_line_tweak` (built and run by CTest);
  the codegen check of a `Tweak<float>` read; a reload that rewrites a file on disk and
  observes the new value (an included file too, and a broken edit that changes nothing);
  pushing then removing a layer giving a structurally equal tree and every tweak's value
  back; `source()` naming layer, file, and line for values from several layers, a merged
  file, an overlay, and a live edit; a shipping-only test (CI's shipping job) that reload is
  inert and a `dev_only` tweak keeps its default; write-back producing a one-line diff with
  the note and comments intact, and refusing a macro-sourced value with its file and line.
- **Property test.** Every value in the corpus's layered config (`config_app.dta`, which
  `#include`s `macros.dta` and `#merge`s `config_default.dta`) is set live and written back:
  10 are written as exactly one changed line in the right file and read back; 4 are refused,
  each a macro (`TRUE`, `kDefaultVolume`, `kMaxPlayers`) named with file and line.
- **Fuzzing.** The parse fuzzer, now also checking edit-mode spans, ran 1.5M execs clean
  under ASan+UBSan.
- **Size: over budget.** `moira::data` is 4,002 counted lines, up 1,284 in M3, which crosses
  the spec's 4,000 (soft, D2). The largest parts are `config.cpp` 571 (write-back about
  200; layers, reload, provenance, live edits the rest), `tweak.hpp`/`tweak.cpp` 347,
  `config.hpp` 125, and `merge.cpp` 82. Flagged here and to the project owner, as §0 asks.
  Six milestones remain. Resolved by D40: the budget is raised to 8,000.

## D40. The line budget is 8,000

*Deviates from spec §0 (4,000) and M10 ("core stays within the 4,000-line budget").
Decided by the project owner, 2026-10-11, after M3 crossed 4,000 (D39).*

The goal has moved from the original's minimal core to a more capable library: layers,
provenance, write-back, and the harness instrumentation of M10 are all beyond what the
70 KB original did. The soft budget for the core (`moira::data` plus `moira::script`,
counted by `tools/count_lines.py`) is now 8,000 non-test, non-comment lines. Everything
else in D2 stands: it is reported in CI, not enforced; each milestone records its count;
and disproportionate growth is still called out. M10's fallback (move the repetition
report into a separate tool if the core is over budget) now applies at 8,000. Edit mode and
write-back stay in the core.


## D41. Spec amendment: telemetry stamps, personal and experiment layers, snapshots, readiness

*Amends spec §4, §10 (new M3b; M9 and M10 changed). Requested by the project owner,
2026-10-11.*

Four additions, folded into the spec text and the milestone table:

1. **Reload telemetry** is split out of M10's loop telemetry. The per-stage stamps (detect,
   parse, merge, bind, notify), `set_clock`, `note_saved`, `mark_visible`, and
   `reload_stats()` move to **M3b**, because they instrument code that exists now. M10
   keeps the aggregation (`loop_stats()`: rolling medians, worst cases, change-failure
   counts) and the edit journal.
2. **Personal and experiment layers** (`push_local_layer`, `toggle_layer` /
   `set_layer_enabled`, `diff`) go in **M3b**.
3. **Snapshot and restore** go in **M3b**.
4. **The readiness run** becomes part of `examples/harness-imgui` in **M9**, with a
   done-when that CI runs it headless.

M3b comes before M4 because all three Config features build on M3 while it is fresh, and
M4 onward does not touch them. Choices made while specifying, where the request left room:

- **The library never learns the user name itself.** `ConfigOptions::user` is supplied by
  the host, consistent with owning no I/O or environment access. "Auto-loaded" means one
  call (`push_local_layer(dir)`) whose missing file is an empty layer that `reload()` picks
  up later. Ignoring `local.*.dta` is a repository-template and cookbook convention; the
  library cannot enforce "never checked in".
- **A disabled layer stays in the stack** (same position, same trees, same source maps),
  so re-enabling is instant and exact. It is skipped by provenance and by write-back's
  supplier search, so `source()` and `write_back()` describe what is actually in effect.
- **`diff` compares the two layers' own trees**, not merged results with and without each.
  That is what "the values two layers disagree on" names, and it works for disabled layers.
- **The clock stays the host's** (spec §4): without `set_clock`, stages are not stamped.
- **Snapshots serialize to DTA** with file layers' trees embedded, so a QA repro saved next
  to an input recording reproduces the values even after the data files change. In
  memory, a snapshot shares trees by reference and costs nothing to take.
- **Toggling and restoring are layer changes**: refused after `freeze()` unless
  `live_layers`, and every one fires the reload callbacks and records a telemetry entry.

The request mentions "the two Moira-shaped rows of the environment slice". That term is
not defined in the spec or the background documents. M9's done-when therefore names what
the run shows instead: edit-to-see time with Moira's share measured, and a tuned value
written back as a one-line diff. If the slice is defined elsewhere, it should be added to
`docs/background/` and M9 should cite it.

## D42. M3b, as specified by the readiness brief (supersedes D41)

*Amends spec §1, §4, §10. Requested by the project owner, 2026-10-11, as a detailed brief
replacing the earlier request D41 recorded. The brief is kept as
`docs/background/m3b-environment-readiness.md`.*

A studio's readiness gate has four data-layer tests: edit-to-see time measured with
Moira's share visible; values tuned in play saved as minimal diffs; experiments that do not
block each other, with A/B comparison; and a return to an exact configuration state. M3b
exposes what Config already holds for each: reload stages, the layer stack, provenance,
and edit-mode spans. The spec now carries the brief's four changes: reload telemetry
(§4 "Reload telemetry"), personal, experiment, and scratch layers with `layer_effect`,
`diff`, and `promote`, snapshots, and the headless test `tests/readiness_loop.cpp` in M3b.
The three readiness panels for `harness-imgui` go in M9.

What changed from D41's version: `set_clock`/`note_saved`/`reload_stats()` became
`set_time_source`, `ReloadRequest::origin_ns`, and `last_reload()`/`each_reload()`, with six
stages instead of five and a default steady-clock source. `ConfigOptions::user` and
`push_local_layer` became a convention (`push_layer("local", ..., {.optional = true})`).
Snapshots no longer serialize to DTA in M3b (an open question below). M9's headless
`--readiness` run is replaced by the M3b test plus the three panels. `mark_visible` and
`loop_stats()` stay in M10.

The brief's decisions, recorded as asked:

1. **Timing uses an injectable `TimeSource`**, defaulting to `steady_clock`. That keeps
   "the library owns no clock" (the host can replace it) while working with no setup.
2. **Tool edits go to a reserved in-memory `scratch` layer** rather than mutating tweaks
   directly. M3's `"live"` layer (D34) is that layer; M3b renames it and adds toggling,
   `clear_scratch`, and the `set` fast path.
3. **Write-back of a scratch-sourced value targets the highest enabled non-scratch layer
   that supplies the path**, refining spec §1's refusal of overlay-sourced values. M3
   already does this for the live layer (D34).
4. **`write_back_to` and `promote` may insert entries, but only as the last child of an
   ancestor array literally present in the target file.** M3's `write_back_to` inserts
   inline before the closing bracket; M3b changes it to a new line indented like the
   previous sibling, and creates an optional layer's absent file.
5. **Every write-back first checks that the file is unchanged on disk since it was
   loaded.** M3 compares the full text held for edit mode, which is exact; M3b keeps that
   and records a content hash for snapshots, which do not hold the text.

Two reconciliations with decisions already made:

- **Line budget.** The brief asks for counts against 4,000; the budget is 8,000 since D40,
  and M3b reports against that.
- **Shipping and layer pushes.** "Layer mutation is inert under `MOIRA_SHIPPING`" is read
  as D38 already defines it: layers can be pushed during the initial load (before
  `freeze()`), and `ConfigOptions::live_layers` remains the explicit live-ops path.
  Toggles, `set`, write-back, and `promote` are inert in shipping, as the brief says.

Open questions, raised before building (spec §11) and answered by the project owner:

- **`layer_effect`** re-merges without the layer on demand; it is measured on the corpus
  during M3b rather than maintained incrementally.
- **Reload history** is a runtime setting, `set_reload_history(n)`, default 256 records,
  allocated on the first record.
- **Snapshots stay in memory** in M3b; serializing them to `.dta` is deferred.
- **The `local` layer convention** goes in `docs/cookbook.md` and is used by the M9
  harness.
- **Scratch lookups are two-tier.** `get()` consults scratch before the merged tree, so
  `set()` stays allocation-free; `root()` is documented as not including scratch edits made
  since the last full merge.
- **The `#include`/`#merge` refusal covers insertion only**: write-back will not insert
  into an ancestor array that is not written in the target file, but still replaces a
  literal token wherever it lives, as M3 does.
- **`set` keeps its multi-value forms** (span and typed overloads) beside
  `set(path, Node)`.


## D43. M3b implementation notes and results

*Recorded 2026-10-11.* Choices made while building D42, where the brief left room:

- **"An existing scalar path" means one already in scratch.** The first `set` of a path
  creates its scratch entry and allocates; every later `set` of that scalar changes the
  entry in place and allocates nothing, which is the case a slider or console repeats.
  The fast path updates only the tweaks bound to that path (or to one of its elements),
  does not advance the epoch, and records no `ReloadRecord`; tool edits would flood the
  history otherwise.
- **The stages tile the change.** `end_ns` is the end of the last stage, not a further
  clock read, so `scan + read + parse + merge + bind + notify == end - start` and a
  dashboard can stack them. File reads happen inside the parser (includes are found while
  parsing), so `read_ns` is the time spent in the reader and `parse_ns` the rest.
- **Triggers.** `reload` by default (or the request's), `layer` for pushes, pops, removals,
  toggles, and `clear_scratch`, `restore`, and `write_back` for write-back and `promote`,
  which re-read the files they wrote and time that.
- **Callbacks on restore** follow D36: a group's callback fires once if its subtree differs
  from before the restore, and not at all if it does not.
- **Disabled layers** keep contributing their macros to the layers above, which were parsed
  with them; toggling re-merges trees and reads no files. Removing a layer still re-reads
  the layers above (D35).
- **Write-back reports.** `write_back` and `write_back_to` return a `WriteReport` or a
  single located refusal; `promote` returns every refusal in `WriteReport::refused` and
  writes nothing then. Written lines are read from the re-read layer, so they are exact even
  when several entries landed in one file.
- **Insertion** puts the entry on a new line indented like the previous sibling when the
  ancestor spans lines (after the rest of that sibling's line, so a trailing comment stays
  with it), on the same line when the ancestor is written on one line, and as a new top-level
  form at the end of the file for the root. The text is the pretty-printer's.
- **Creating an optional layer's file** needs its directory. The default writer creates
  missing parent directories (`mkdir` / `_mkdir`), the one place the library creates
  directories; it still uses no `std::filesystem` (D22). A host writer is unaffected.
- **Effect and diff values** of several nodes are an untagged array, `(1920 1080)`, and
  paths are dotted with quoting where needed, so `get<T>(entry.path)` takes them. `promote`
  accepts `scratch` as its source; nothing is ever written to scratch.
- **Snapshots** share every layer's trees, source maps, and recorded file contents, and copy
  scratch's arrays, which are small and change in place.
- **On-demand `layer_effect`** (D42) measured on the corpus stack (`config_app.dta` and its
  includes, plus a 200-entry experiment layer), Release, gcc 13: 41 µs for the base layer,
  65 µs for the experiment, 49 µs for `diff` between them. Tooling-fast; no incremental
  upkeep needed.

Results:

- Every acceptance item in the brief is a test (`tests/data/test_m3b.cpp`), as is the
  headless readiness loop (`tests/readiness_loop.cpp`). They include zero allocations for a
  repeated `set` and for tweak reads, and an equal allocation count for reloads with and
  without the history ring.
- **Matrix.** All twelve local configurations pass (gcc and clang at C++20 and C++23,
  libc++, data-only, ASan+UBSan, TSan, the plain reference-count build, shipping, and
  no-exceptions on gcc and clang). Two fixes were needed: helpers used only by non-shipping
  code are marked `[[maybe_unused]]`, for clang's `-Wunneeded-internal-declaration` and
  gcc's unused-function warning in the shipping build. MSVC is covered by CI. Its debug STL
  allocates more per container copy, so the snapshot test compares a 2-entry and a
  500-entry config instead of bounding the count.
- **Size.** `moira::data` is 4,747 counted lines (core 4,759), up 745 in M3b. That is over
  the 4,000 the brief cites and under the 8,000 budget of D40.

## D44. Functions and variables live in per-Context tables (supersedes D5)

*Spec §5, "Handler caching"; M4 plan Q1, answered 2026-10-11.*

Every symbol carries a dense 32-bit index (`Symbol::index()`), stored in padding
`SymbolEntry` already had, so symbols cost no more memory. Each Context keeps two tables
indexed by it:

- **Functions.** A flat array of function records. Resolving a command's head is a load of
  the symbol's index, a bounds check, and a load: 1.2 ns against 4.8 ns for a hash map
  (`bench/bench_call.cpp`).
- **Variables.** Value cells in pages of 1,024, allocated when a variable in that range is
  first used. Reading `$x` is the index, the page, and the cell: 7–8 ns through
  `try_eval`, almost all of it the call itself. A `VarRef` holds the cell's address.

D5's in-node cache is not built. It saved a lookup that now costs about a nanosecond, and
it had three costs this avoids:

- it modified the tree on first run, which conflicts with sharing trees between threads
  and with macros sharing arrays;
- a second Context would keep re-resolving the first one's cached handler;
- `diff`, `layer_effect` and structural equality would see a `Func` node where the file
  has a symbol.

Evaluation never modifies a tree, so separate Contexts can run the same trees at once.
The cost is memory proportional to the highest symbol index used, per Context: 8 bytes
per symbol for functions, and 16 KB per page of variables.

## D45. Typed lookups evaluate, in the thread's current Context

*Spec §5, "Evaluation semantics"; M4 plan Q2.*

`find_int`, `get<T>` and the other typed lookups evaluate a Command or Var value, as §5
says. `moira::data` stays independent of `moira::script` through one hook:
`detail::set_evaluator`, installed by the first Context constructed.

- **Only on the slow path.** A lookup converts the raw values first. Only if that fails,
  and some value is a Command or Var, does it evaluate them and convert again. Config
  lookups cost what they did. Only the values themselves are evaluated, not arrays nested
  inside them.
- **Raw reads stay raw.** `find`, `at`, `Convert<Node>` and `find<Node>` never evaluate.
- **Errors.** A value that evaluates to the wrong type is reported as what it evaluated
  to: `'name' is symbol, not int`. Without `moira::script`, the M2 message stands.
- **Which Context.** The calling thread's current Context:
  - the innermost `Context::Scope`;
  - else the Context running the current `try_eval`;
  - else the first Context created on the thread, which stays current until destroyed.

  With none, the lookup fails cleanly: "needs a current moira::Context on this thread".

Inside a native function, `Args::get<T>(i)` evaluates its argument; `args.raw(i)` does
not. A `Node` parameter is evaluated; `Raw<Node>` is the unevaluated one.

## D46. M4 scope: script functions and the built-in vocabulary

*Spec §10 M4, M5; M4 plan Q3 and Q4.*

- **Script functions.** `{func}`, `{override}`, and their `(status ...)` and `(doc ...)`
  metadata are built in M4 rather than M5, because M4's "a script stub replaced by a native
  registration" needs one. `{class}` and `{new}` stay in M5.
- **Built-ins.** Every Context registers a control set and a calculator core, through the
  same `func` as user code:
  - control: `if`, `set`, `do`, `func`, `override`;
  - arithmetic: `+ - * / %`;
  - comparison: `== != < <= > >=`;
  - logic: `and or not`.

  They are in `docs/script-reference.md`.
  - Ints stay ints and wrap on overflow (no undefined behavior). `/` always gives a float,
    and `%` by zero is an error.
  - `==` compares numbers by value (`1` equals `1.0`) and anything else by structure.
  - `and` and `or` stop at the first argument that decides the result.

## D47. M4 implementation notes and results

*Recorded 2026-10-11.* Choices made where the spec left room:

- **`Args` counts from 0 after the function name**, so `args.get<int>(0)` is the first
  argument. Error messages count from 1 ("argument 1 of 'not'").
- **Overload sets evaluate every argument once, then choose.** Choosing by runtime type
  needs the values, and evaluating twice would repeat side effects. So a function taking
  `Raw` parameters cannot be overloaded (a registration error). `Args` in an overload sees
  the evaluated values. A function with one signature passes its arguments through
  unevaluated, which keeps the common call path lazy and fast.
- **Ambiguity is checked at registration** as "same arity and same parameter types". This
  is stricter than real ambiguity, but resolution is first match, so nothing reachable is
  lost.
- **Unset variables are errors**, `$x is not set`, rather than reading as 0: silently
  using a default is what located errors exist to prevent (D25). `{do ($a) ...}` binds
  `$a` unset; `($a value)` binds a value. Bindings are sequential.
- **Script functions** evaluate their arguments in the caller's bindings, then bind the
  parameters, then run the body, which returns its last value.
- **Collisions** follow §5:
  - A native registration replaces a script definition, with a notice that compares the
    arities.
  - `{func}` over a native name is an error naming `{override}`.
  - `{override}` replaces the native, with a notice.

  Notices go to a sink, stderr by default (`set_notice_sink`).
- **Tiers.** `Tier` has Native and Script only. Macros are expanded by the reader before
  anything runs, so no function record is ever a macro.
- **Safety.** Deep recursion is a `Limit` error, not a stack overflow.
  - The limit is 1,000 nested calls, or `set_stack_budget` bytes of stack below the
    outermost call (default 256 KB), whichever comes first. A call count alone was not
    enough: MSVC debug frames overflowed Windows' 1 MB stack before 1,000 calls (CI caught
    it).
  - In the 256 KB budget, release builds nest about 440 calls and gcc debug builds about
    290.
  - An exception thrown by a native function comes back from `try_eval` as its error, and
    the dynamic scope is restored on every exit path.
- **Small frames.** The evaluator's frames nest once per script call, so they hold no
  error-message temporaries: errors are built in out-of-line cold functions, and a bound
  function's arguments are read into one shared result. That took `call()` from 720 bytes
  of stack to 128, and also made calls faster.
- **`check`** reads a Context's signatures and the `{func}` definitions in the trees it is
  given. It is a free function: the spec's "opt-in via a Config flag" waits for `#require`
  and shapes (M8b), where checking at load has a natural home.
  - It also checks `Raw<Var>` slots ({set x 1} is reported), because a literal there is
    knowably wrong.
  - Object dispatch, unknown methods and the one-way rule arrive with M5 and M8b.
- **Not yet.** Evaluating `[...]` property nodes and object commands (`{{nearest} hit}`)
  are errors until M5.
- **The reference** is `docs/script-reference.md`, generated from a default Context.
  - `tests/script/test_check.cpp` fails when it drifts.
  - Running the script tests with `MOIRA_UPDATE_REFERENCE=1` rewrites it.

Results:

- **Done-when items.** Each is a test in `tests/script/`, and the calculator example
  (`examples/calculator`) runs under ctest:
  - `if` and `set` are table entries, and the evaluator source names no function;
  - the untaken branch of `if` is never evaluated;
  - the reference drift test;
  - `check` finds a wrong-arity call without evaluating;
  - a stub replaced by a native function runs unchanged call sites;
  - the demand report lists unresolved heads with every call site.

  Allocation tests show zero allocations for native calls, overload sets, variables,
  `if`, `do` and `set` after warm-up.
- **Benchmark** (`bench/bench_call.cpp`, Release, gcc 13, this container). All rows make
  zero allocations.

  | Call | Time |
  |---|---:|
  | `{add 1 2}`, 2 int args | **25.6 ns** (target 40) |
  | Overload set `{+ 1 2}` | 43 ns |
  | Promoted `{+ 1.5 2}` | 48 ns |
  | `{add $x 1}` | 32 ns |
  | `{if 1 2 3}` | 30 ns |
  | Script `{add1 1}` | 77 ns |
- **Matrix.** All twelve local configurations pass. One fix was needed: a converter used
  only by exception-only tests tripped clang's unused-variable warning in the
  no-exceptions build.
- **MSVC (CI).**
  - C4702 (unreachable code) flagged an `if constexpr` with no `else` in a header
    template.
  - The recursion test overflowed the stack in debug builds; see Safety.
- **Size.** `moira::data` is 4,807 counted lines (+48, the hook and the symbol index).
  `moira::script` is 1,396. The core total is 6,203 of 8,000, up 1,444 in M4, under the
  plan's 1,600–1,750 estimate. That leaves 1,797 lines for M5–M8 and M10's core
  instrumentation. As the plan said, this is likely to run out around M8, and each
  milestone will flag it.

## D48. The line budget is 10,000

*Deviates from spec §0 and M10 again; supersedes D40's number. Decided by the project
owner, 2026-10-11, in the M5 plan (Q4).*

M4 brought the core to 6,203 lines. The M5 estimate (~780) and rough ones for M6–M8 and
M10's core instrumentation (~1,500) would have exhausted 8,000 during M7. The soft budget
for the core is now 10,000 non-test, non-comment lines. The rest of D2 and D40 stands:
- the budget is reported, not enforced;
- each milestone records its count;
- disproportionate growth is still called out;
- M10's fallback (move the repetition report into a separate tool) applies at the new
  number.

## D49. Objects: table, handles, binding, and lifetime

*Spec §5 ("Objects"); M5 plan, Q1 answered 2026-10-11.*

- **Lifetime.**
  - `ctx.add(obj[, name])` adds an object.
  - `ObjectBase` (the base of every `Object<T>`) keeps its Context and handle, and its
    destructor removes it.
  - A Context destroyed first leaves its objects unregistered: they can join another.
  - A copy of an object is in no Context.
  - Objects created by `{new}` are owned by the Context and deleted with it or by
    `{delete}`.
- **Handles** (D4, D19).
  - A slot holds the object, its name, and a salt that starts at 1 and increments when the
    slot is freed, so a handle to a removed object resolves to nothing.
  - `try_resolve` / `try_get<T>` are the weak forms and return null. `get<T>` is the
    strong form: it throws "the Person object was removed", or "the object is a Hero, not
    a Person".
- **Names.**
  - A named object is found through a per-Context table indexed by `Symbol::index()`, like
    functions (D44).
  - Functions take precedence at dispatch.
  - Naming an object after a function, or after a live object, is refused.
- **Inheritance through the second template argument.** `Object<T, Base>` gives every
  object one `ObjectBase`, so a base type's method thunks can downcast from it safely.
  - `T`'s table starts as a copy of `Base`'s, so dispatch never walks a chain.
  - A method `T` binds replaces the inherited one of the same name.
  - `b.inherits<B>()`, the spec's spelling, is accepted and checked, but the template
    argument already did it.
- **No RTTI.**
  - Each type's `TypeRecord` is a function-local static, built thread-safely on first use
    from `T::bind`.
  - Its default name is `T`'s unqualified name, read from the compiler's signature of a
    template function (`__PRETTY_FUNCTION__` / `__FUNCSIG__`). `b.name()` overrides it.
  - `try_get<T>` compares records along the flattened ancestor list.
- **Methods reuse M4's binding.** A member function pointer, or a callable taking `T&`
  first, is wrapped into a function whose first parameter is the object, which takes no
  argument (`ParamKind::Implicit`, as `Context&` does). So methods get signature
  deduction, `Raw<T>`, optional and rest parameters, overloads, docs, and status.
- **Method tables** are static per type, sorted by symbol identity for binary search.
  Properties are a short list searched linearly.

## D50. Script handlers, `$this`, properties, and parameters

*Spec §5; the talk's `DataClass`; M5 plan Q2 and Q3.*

- **`{class Name (method ($params...) body...) (method body...) ...}`.**
  - If `Name` is a bound type, the handlers apply to its objects and to objects derived
    from it.
  - Otherwise it is a script class, for `{new}`.
  - Classes are matched to types by name when a command is sent, so a class may be defined
    before the type is first added.
  - Defining a class again replaces its handlers, as reloading a script would.
- **Dispatch order.**
  1. Script handlers, most derived type first. This lets data override or extend a C++
     type without a rebuild.
  2. Native methods.
  3. The methods every object has: `get`, `set`, `type`, `name`.

  A handler's arguments are evaluated in the caller's scope, then `$this` and the
  parameters are bound by the same dynamic scope as `do`.
- **Properties and parameters.**
  - `{obj get k}` and `{obj set k v}` reach a bound property first, then the object's loose
    parameters (the talk's "Perl dictionary"). Parameters are allocated on first `set`.
  - `[k]` reads `$this`'s; `[a b]` reads `b` from `a`, where `a` is an object or an array
    of tagged values.
  - `{set [k] v}` assigns one: `Raw<Var>` is now "a place", a variable or a property.
- **`{new Class [name] (param value)...}`** creates a script-class instance, or a
  default-constructible bound type, as `rec.create` allows.
- **`{delete obj}`** also accepts a bare name.
- **`[...]` at a file's top level is still a macro (D11).** A property expression must be
  inside a command or array.
- **Object commands evaluate slot 0** when it is a command or variable; a value that is
  not an object is an error.
- **Not in M5.**
  - Script classes do not derive from one another.
  - A redefinition of a class while one of its handlers runs is not guarded against.

## D51. M5 results

*Recorded 2026-10-11.*

- **Done-when items.** `tests/script/test_object.cpp` covers each of them:
  - the talk's `Person::grow` runs from script (`{bob grow 10}`), through the same dispatch
    from C++ (`ctx.call(bob, "grow", 1.5)`), and directly;
  - C++ calls a handler defined only in script (`ctx.call(bob, "miss")`);
  - script handlers take precedence over native methods.

  The tests also cover inheritance, stale handles and slot reuse, slot-0 evaluation,
  located errors naming type and method, parameters and property paths, script classes
  with `new`/`delete`, and a Context dying before its objects.
- **Allocations.** A native method call, `get`, and `set` allocate nothing after warm-up.
- **Tooling.**
  - The reference gains a Types section: methods with signatures and docs, properties,
    base types, attached script handlers, and script classes.
  - `check` checks the arity and literal types of methods sent to named objects and to
    objects the tree creates, and reports their unknown methods in the demand report as
    "'fly' on Person". `CheckReport::Demand` gains `on`.
- **Performance.** On this container, against the M4 build measured on the same machine:
  - `{add 1 2}` is 31 ns for both (target 40).
  - `{bob grow 0}`, a native method returning a float, takes 84 ns.
  - Two regressions were found and fixed:
    - routing every call through the method-capable `invoke_set` added about 65
      instructions, so the single-overload function path is inline again;
    - converting a float result to its shortest decimal took about 150 ns through
      `snprintf`. `float_to_decimal_double` now has an arithmetic fast path, which agreed
      with the text search on 2.9 million floats, and keeps the search as a fallback.
- **Matrix.** All twelve local configurations pass. ASan initially failed the recursion
  tests: its fake stack keeps address-taken locals on the heap, so measuring the stack
  budget (D47) from a local's address saw nonsense. The budget now reads the frame
  address (`__builtin_frame_address`, `_AddressOfReturnAddress` on MSVC).
- **Size.** The core is 7,142 of 10,000 lines (D48), up 939 in M5: `moira::script`
  +917, `moira::data` +22. That is about 160 lines over the plan's estimate of 780. The
  overrun is mostly property paths, `{new}` for native types, and the reference and
  `check` additions.

## D52. The script call stack

*Spec §5 ("Debugging"); M6 plan, step 1. No open question bears on it.*

- **Frames.** `Impl::call` keeps a `CallFrame` (the command, a parent pointer) in the
  frame it already had for the depth count, and `Impl::top` points at the innermost one.
  That is two stores in and one out per call, with no allocation. `ctx.top_frame()` exposes
  the list read-only, for a debugger to walk.
- **Errors carry the stack.** `Error` gains `stack`, a vector of `StackEntry` (function,
  the object's name for a method, file, line), innermost first. It is filled once, on the
  error path only:
  - where the evaluator makes an error (`fail`);
  - where an error first passes a call (`locate`, which every propagating error already
    went through).

  An error that already has a stack keeps it as it unwinds further. A call that failed
  before it started (an unknown function, an arity error, too deep) is not on its own
  stack; its error is located at it.
- **Exceptions.** The plan was to catch `moira::Exception` around each native thunk.
  Measured, a `try` there added about 10 ns to every native call, so it is not done.
  Instead, `moira::data` has a stack-recorder hook (as it has an evaluator hook, D45), and
  the `Exception` constructor calls it. An exception thrown from a native function records
  the frames it is thrown through, at the throw, and costs nothing until then.
- **Rendering.**
  - `to_string(error)` stays one line.
  - `describe(error)` adds `  in {bob attack}  (boss.dta:4)` per frame.
  - `Exception::what()` and the default notice sink use `describe`.
  - `script_backtrace()` renders the live frames of `Context::current()`.
  - `ctx.backtrace()` returns them as `StackEntry`s.
- **Cost.** `{add 1 2}` went from 30 to 34 ns on this container (target 40). About half
  of that is the two stores and the restore, and half is the larger frame. A nested
  evaluation inside a native function shares the Context's list, so its error records the
  outer calls too.

## D53. M6: messages, statistics, and tracing answers

*M6 plan Q1–Q3, answered by the project owner 2026-10-11 after the `acton-review` lens
was written.*

- **Q1, messages.** `obj.handle(msg)` returns `Expected<Node>`, and an unhandled message
  is a `NotFound` error. `obj.handles<M>()` is a cheap check that allocates nothing.
  - **New:** `handle_all(Span<ObjectBase*>, msg)` broadcasts one message.
    It groups the objects by type, so the table search and the "is there a script
    override?" check happen once per type, not once per object ("where there is one,
    there are many"; no last-minute decisions).
- **Q2, statistics.** Non-shipping builds always count calls, with one increment beside
  the function record. Timing is off until `ctx.set_profiling(true)`. All of it compiles
  out under `MOIRA_SHIPPING`.
- **Q3, tracing.** Compiled out under `MOIRA_SHIPPING`. The API still compiles and does
  nothing.

M6's remaining steps (messages, statistics, tracing) wait for the hygiene work in D54–D56
(`docs/m5b-plan.md`), so they are built on the new API from the start.

## D54. No exceptions, and no `try_` prefix

*Deviates from spec §6 ("every throwing API has a non-throwing twin", `MOIRA_NO_EXCEPTIONS`).
Decided by the project owner 2026-10-11.*

- **The throwing forms are removed.** Every fallible API returns `Expected<T>`. This
  removes:
  - `moira::Exception`;
  - `detail::raise`;
  - the `MOIRA_EXCEPTIONS` and `MOIRA_NO_EXCEPTIONS` switches and the CI job for them;
  - D52's stack-recorder hook, which existed only for exceptions.

  The library builds with exceptions disabled (`-fno-exceptions`, `/EHs-c-`) in every CI
  job. A game that uses exceptions elsewhere can still link it.
- **The `try_` prefix goes too.** With one form there is nothing to tell apart, and the
  one-line API reads better: `ctx.eval`, `ctx.func`, `ctx.add`, `arr.find<T>`, and so on.
  It is one mechanical rename across the API, tests, examples, and docs. `[[nodiscard]]`
  on every `Expected` keeps a failure from being dropped silently.
- **Native functions report failure by returning `Expected`,** as they already could. A
  C++ exception escaping a native function is now the host's bug; Moira does not catch
  it.
- **Why.** A console codebase builds with exceptions off. A library that offers throwing
  forms invites them into game code, and that is the entanglement the TD guards against
  (`acton-review`, "Why the line is held"). Having one form also halves the API.

## D55. Public headers are free of the standard library

*Deviates from spec §6's silence on the subject (it requires only no iostream). Decided by
the project owner 2026-10-11 (gap 11 of `acton-review`).*

- **Public headers may include:**
  - the language-support headers: `<cstddef>`, `<cstdint>`, `<type_traits>`, `<new>`,
    `<bit>`, `<atomic>`, `<initializer_list>`, `<compare>`, `<concepts>`, `<utility>`, and
    `<limits>`;
  - the header-only, non-allocating standard vocabulary types: `<string_view>`, `<span>`,
    and `<optional>`;
  - `<expected>` where D21 chose it.

  That rules out every container, `std::string`, `std::function`, `std::shared_ptr` and
  `unique_ptr`, `<memory>`, `<functional>`, `<algorithm>`, and `<tuple>`. (The owner first
  chose Moira-owned vocabulary types, then decided the same day that the std ones are fine:
  they do not allocate, and reimplementing them buys nothing.)
- **`Error` becomes a handle.** It is an owning pointer to an error record (code, message,
  location, path, stack) allocated through the allocator (D56) on the error path only.
  `Expected<Node>` shrinks from about 120 bytes to 24, which also closes gap 9.
- **Callbacks** are a function pointer plus `void*`, as `NoticeSink` already is. No
  `std::function`.
- **Inside `src/`,** containers go through one internal alias layer (`src/common/containers.hpp`)
  that maps to the STL by default, or to EASTL with `MOIRA_USE_EASTL=ON`, always with
  Moira's allocator (D56). This follows the owner's rule: where reimplementing an STL
  class would otherwise be needed, at least allow EASTL instead
  (<https://github.com/electronicarts/EASTL>). Naked `std::` containers in `src/` are
  not allowed.
- **Checked in CI:** a test compiles each public header alone and fails if it reaches any
  standard header outside the allowed list. It checks the preprocessor's dependency
  output, not by eye. An EASTL job builds with `MOIRA_USE_EASTL=ON`.
  EASTL is fetched for that job only and is not vendored.

## D56. Per-Context allocators, and an arena per load

*Gap 1 of `acton-review`. Decided by the project owner 2026-10-11.*

- **The interface** is a plain struct of function pointers:
  `struct Allocator { void* (*alloc)(void* user, size_t size, size_t align); void (*free)(void* user, void* p, size_t size); void* user; };`.
  No virtual calls, and no STL.
- **Who takes one.** Each `Context` and each `Config` takes one at construction. Every
  allocation it makes goes through it: tables, script state, error records, and the trees
  it loads. Process-wide state (the symbol table) uses a default allocator the host can
  set once, before first use. By default, every allocator is `operator new`.
- **Trees live in an arena per load.** Each load (a file, a `Config` layer, an `eval`
  string) allocates its tree from arena blocks taken from the owner's allocator, and the
  load frees them as a whole.
  - There is no per-array free and no refcount inside a tree. The tree has one owner,
    which also closes gap 5 and lines up with M8's load-in-place.
  - Arrays built after load (`set`, macro expansion, script-built values) go to the
    owner's scratch arena, which is collected at points the host chooses.
- **Open, for the plan:** how values that outlive their load (a Node held in a variable,
  a snapshot, D42) keep their tree alive. Candidates: a per-load refcount, with one count
  per load rather than per array, or an explicit copy into the scratch arena.

## D57. Hot and cold layout fixes wait for M8

*Gaps 2, 2b, and 3 of `acton-review`. Decided by the project owner 2026-10-11.*

The following are done together with the binary cache, while the tree layout is being
redesigned anyway:
- splitting method and function records into hot and cold parts;
- moving the thunk and callable pointer into the dispatch table;
- storing a run of contiguous tags beside a tree's children.

M8's plan carries them, along with cold-cache benchmark rows (a working set larger than
L2) to measure them.

## D58. Task frames come from a pool with a frame budget

*Gap 7 of `acton-review`, for the M7 plan. Decided by the project owner 2026-10-11.*

- M7's coroutine promise type defines `operator new`, which draws from a per-Scheduler
  pool of fixed-size blocks, taken from the Context's allocator (D56).
- The frame budget is the block size. A frame's size is known only when it is allocated,
  so an oversized frame fails at spawn: `get_return_object_on_allocation_failure` turns
  it into an error naming the task. It never falls back to the heap.
- The Scheduler steps tasks in a batch. Structural changes made during a step (`{new}`,
  `{delete}`, registration) are queued and applied at a named sync point.

## D59. M5b step 1 results: exceptions and `try_` removed

*Recorded 2026-10-11. Implements D54.*

- **What is gone:**
  - `Exception`, `detail::raise`, and every throwing form;
  - `MOIRA_EXCEPTIONS` and `MOIRA_NO_EXCEPTIONS`, and both "no exceptions" CI jobs;
  - D52's stack-recorder hook.

  Every target builds with `-fno-exceptions` (`/EHs-c-` with `_HAS_EXCEPTIONS=0` on MSVC):
  the library, tests, examples, tools, and benchmarks. doctest runs in its no-exceptions
  mode, so a failed `REQUIRE` ends the test binary.
- **What `try_` means now.** It marks only weak lookups, which return null or an empty
  optional and build no error:
  - `Node::try_int`/`try_float`/`try_sym`/`try_str`/`try_array`;
  - `Array::try_at`, `try_find(tag)`, and `try_get(path)`;
  - `Context::try_resolve` and `try_get<T>(object)`.

  Everything that returns `Expected` lost the prefix: `read_file`, `eval`, `func`, `add`,
  `call`, `find<T>`, `find_float`, `get<T>`, `push_layer`, `reload`, `set`, `write_back`,
  and the rest.
- **Strong accessors return `Expected`.** `Node::as_int()` and the other `as_X()` and
  `as<T>()` accessors return `Expected<X>`, with the same located message as before.
  `as_array()` returns `Expected<const Array*>`. `Context::get<T>(object)` returns
  `Expected<T*>`. `Array::at(i)` and the reference-returning `Array::find(tag)` and
  `get(path)` are gone: `try_at`, `try_find`, and `try_get` cover them.
- **Renamed internally:** the private `Context::add(Symbol, Overload)` became
  `add_overload`, so `add` is only the public object form.
- **Tests.** About 700 lines of tests were written against the throwing forms. They were
  converted, not dropped:
  - `tests/support/ok.hpp` adds `ok(expected)`, which returns the value or fails the test
    with `describe(error)`, script stack included;
  - assertion counts match the previous build within the merged duplicates (673 against
    674 script assertions, and 75,959 against 75,963 data assertions).

  Five `CHECK_THROWS` checks would have compiled to nothing without exceptions. They now
  check the returned error.
- **A latent issue surfaced.** One test called `set` and `write_back` in the shipping
  build, where both are refused. The throwing form had hidden it. The test now expects
  the refusal under `MOIRA_SHIPPING`.
- **Performance, on this container:**
  - `{add 1 2}` is 32–34 ns, unchanged (target 40, zero allocations).
  - **`-fno-exceptions` itself slows lookup.** HEAD's own benchmark, built with
    `MOIRA_NO_EXCEPTIONS=ON`, reads `try_find_float` on a 20-child array at about 17 ns
    on average and 29 ns for the last key. With exceptions on, the same build reads 17 and
    24 ns. The new library matches the no-exceptions numbers exactly (17.3 and 28.9 ns
    with that benchmark file). The cause was not investigated: D57's M8 layout work
    rewrites this path.
  - **The rewritten `bench_lookup.cpp` reads about 3 ns slower than HEAD's file** against
    the same library (20–21 ns average, 31–32 ns last), because of inlining in the
    benchmark's own translation unit, not the library.
  - The §7 target (≤ 25 ns, read as the average) is met either way. The last-key row
    missed 25 ns at HEAD too.
- **Size.** The core is 6,933 lines, down 329 (D54 estimated −150).

## D60. M5b step 2 results: public headers without the standard library

*Recorded 2026-10-11. Implements D55, except for its allocator-related parts (step 3).*

- **The rule holds and is enforced.**
  - Public headers reach only D55's allowed headers. `tools/check_headers.py` (CTest
    `moira_header_allowlist`) walks each header's include closure and fails on anything
    else.
  - A CMake object library compiles every public header on its own.
  - The count went 81 → 59 → 53 → 51 → 24 → 17 → 0 over sub-steps 2a–2f.
  - The allow-list also admits the trivial C headers `<cstdlib>`, `<cstring>`,
    `<cassert>`, and `<climits>`, and `<version>`.
- **The cost to a game file.** A translation unit that only reads a `Tweak<float>`
  preprocesses to 35,005 lines instead of 81,222 (the allowed headers alone are 31,434),
  and compiles at `-O2` in about 0.5 s instead of 1.35 s.
- **Opt-in interop headers.** Headers named `std_*.hpp` are exempt by name, and nothing
  else in Moira includes them:
  - `<moira/std_interop.hpp>` (data): `std::hash<Symbol>`, `Convert<>` for
    `std::string`/`vector`/`array`, `string_out()`, `to_string()` for nodes, arrays,
    paths, and stack entries, `to_dta()`, and `strings(List<Symbol>)`;
  - `<moira/std_script.hpp>` (script): `to_string(CheckReport)`, `reference(ctx)`, and
    `script_backtrace()` returning strings. It is separate so that `moira::data` never
    includes a script header.
- **New small types.** Each is a minimal piece of what the STL would provide, and each
  is tested:
  - `TextOut` (`text.hpp`): a function pointer and a context. All text Moira produces is
    written through it: `write(node)`, `write_dta`, `Path::write`, the check report, the
    reference, the backtrace, and file readers.
  - `Callback<Sig>` (`callback.hpp`): an owning, copyable callable for stored host code
    (`on_reload`, `on_change`, readers, writers, warnings). A capture-free lambda or a
    function is stored by pointer with no allocation.
  - `List<T>` (`list.hpp`): a fixed-size owning array for results (`layers()`,
    `unsaved()`, `layer_effect`, `diff`, `WriteReport`, `CheckReport`, `backtrace()`).
  - `detail::NodeBuffer`: 8 nodes inline, heap beyond; it backs `ValueList` and evaluated
    lookups.
  - `detail::Shared`: an intrusive refcounted holder for a function's callable. `get()` is
    one load, as `shared_ptr::get()` was.
- **API changes,** all mechanical for a caller:
  - `Error` is a handle with accessors (`e.message()`, `e.file()`, ...). `to_string(e)`
    and `describe(e)` return null-terminated views cached in the error. `Expected<Node>`
    is 24 bytes.
  - `LoadSession` is opaque: `add_include_dir()` and `autorun()`. Readers have the
    signature `bool(std::string_view path, TextOut out)`.
  - `SourceMap::spans()` and `text()` return `std::optional` views.
  - `Path` holds up to 16 segments inline; deeper is invalid, and an invalid path keeps
    the first 64 characters of its text. `to_string(path)` moves to interop.
  - `Source::layer`, layer names in results, and the paths in `LayerEffectEntry`/
    `DiffEntry` are `Symbol`s.
  - `TweakBase::values(ValueList&)` fills instead of returning a vector.
  - `ObjectBase::type_name()` replaces reading the type record. `TypeRecord` is defined
    only in `src/`, and `Binder` reaches it through out-of-line functions.
  - Parameter descriptions are `static constexpr` tables, and a signature's types are a
    `TypeList` rather than a `std::tuple`.
  - `script_backtrace(TextOut)` allocates nothing: line numbers are formatted on the
    stack.
- **Performance, on this container:**
  - `{add 1 2}` 33–34 ns, `{add1 1}` 108–112 ns, and `{bob grow 0}` 86–89 ns; all
    unchanged, with zero allocations.
  - `find_float` on 20 children: 19–20 ns average.
  - Parsing a path string in `get<T>("...")` dropped from about 200 ns to 147 ns,
    because `Path` no longer allocates.
  - 2c's apparent 3–4 ns slowdown of prebuilt-path lookups disappeared in 2d with no
    related change, so it was code layout, not `Path`.
- **Process.** Two slips were caught after a commit, and the local matrix gained
  configurations so neither recurs:
  - shipping-only test code broke in 2a;
  - a GCC `-O3` maybe-uninitialized false positive appeared in 2e.

  The matrix is now debug, release, C++23, shipping, ASan+UBSan, Clang, data-only, and
  plain refcounts.
- **Size.** The core is 7,633 lines, up 700 in step 2. The plan estimated +150, without
  counting the new small types, opaque implementations, or interop headers. With step 1's
  −329, M5b so far is +371 against the plan's +450 for all four steps. Steps 3–4 remain.

## D61. M5b step 3 design: who allocates from which allocator

*Recorded 2026-10-11, before step 3's code. Settles the handoff's open point (which
allocator the header types use) and the rules step 3 follows. Implements D55's alias layer
and the first half of D56.*

- **The rule.** What an owner keeps comes from the owner's allocator. What it hands back,
  and what has no owner, comes from the process-wide default allocator.
  - **The owner's allocator** (`Context(alloc)`, `Config(options, alloc)`): the `Impl`
    itself, every table and record it keeps (functions, variable pages, object slots,
    classes; layers, sources, edits, tweak bindings), and their containers.
  - **The default allocator:**
    - the header types `List`, `Callback`, `detail::NodeBuffer`, and `detail::Shared`;
    - `Error` records;
    - snapshots and other results handed to the host;
    - temporaries that die before the call returns;
    - process-wide state: the symbol table, `TypeRecord`s, and the global tweak registry;
    - host-made objects with no owner: `LoadSession`, `SourceMap`, a `Tweak`'s side data;
    - trees (`Array`, `String`), until step 4 gives each load an arena.
- **Why the default allocator for the header types.** They are built where no owner is in
  reach: a `List` is returned, a `Callback` is made by the host before it is passed in, a
  `NodeBuffer` spills during evaluation. Asking for an owner would add a pointer to each
  and a choice at every copy (whose allocator does the copy use?). Results also outlive
  their owner: a host may keep a `List` or an `Error` after the `Context` is gone, so they
  must not depend on the owner's allocator living that long. The headers call two
  out-of-line functions, `detail::default_alloc` and `detail::default_free`, so they still
  include nothing new.
- **The interface** is D56's struct, in `<moira/allocator.hpp>`. An allocator must not
  return null. If one does, Moira aborts, as `operator new` failing did with exceptions
  off. `free` receives the size that was allocated.
- **The default allocator** is `operator new` unless the host calls
  `set_default_allocator()` before Moira first allocates. After that it returns false and
  changes nothing, because blocks already handed out must be freed where they came from.
- **Lifetime.** An owner's allocator must outlive the owner and anything copied out of it
  (a `ConfigSnapshot`). Copies keep their source's allocator, as both the STL and EASTL
  do, so no copy ever frees into the wrong heap.
- **Attribution is by table, not by every byte.** The owner passes its allocator to its
  tables and to the records it creates. A container that is built elsewhere and moved
  into a record (a temporary, a parsed name) keeps the allocator it was built with,
  usually the default. Plumbing the owner into every nested string would mean an
  allocator argument at every construction, for no change in what reaches the heap.
- **The alias layer** is `src/common/containers.hpp`. `Vec<T>`, `Str`, and `HashMap<K, V>`
  are the STL's by default, or EASTL's with `MOIRA_USE_EASTL=ON`. Each holds a pointer to
  a Moira `Allocator`, which is the default allocator unless the owner passes its own.
  `Str` is a thin class over the backend string, because EASTL's string does not convert
  to `std::string_view`. Naked `std::` containers and `std::string` are gone from `src/`,
  along with `std::to_string`, `make_unique`/`make_shared`, and the `std_*.hpp` interop
  headers.
- **Tested** in its own test binary, whose `main` installs a counting default allocator
  before anything else runs. A `Context` and a `Config`, each given its own counting
  allocator, are built, loaded, evaluated, reloaded, and destroyed. Global `operator new`
  sees no allocation, each owner's allocator sees some, and each is balanced at the end.

## D62. M5b step 3 results: allocators and the container alias layer

*Recorded 2026-10-11. Implements D61, D55's alias layer, and D56's allocators. Arenas are
step 4.*

- **The API.**
  - `<moira/allocator.hpp>` has `Allocator`, `default_allocator()`, and
    `set_default_allocator()`, which returns false once Moira has allocated.
  - `Context(alloc)` and `Config(options, alloc)` each take one, defaulting to the
    default allocator.
  - `{new T}` objects come from the Context's allocator. A type record now carries a
    destroy function beside `create`, because only the type knows its size.
- **What moved off `operator new`:**
  - **Onto the owner's allocator:** the Context and Config `Impl`s and every table in
    them, function records, script classes, variable pages, `{new}` objects, snapshots,
    and each layer's shared session and source map.
  - **Onto the default allocator:**
    - `List`, `Callback`, `NodeBuffer`, `Shared`, and `Array`/`String` trees;
    - `Error` records, sessions, source maps, a `Tweak`'s side data, and loose object
      parameters;
    - the symbol table, whose blocks and slots are now raw default-allocator memory, and
      `TypeRecord`s.
- **The alias layer.** `src/common/containers.hpp` has `Vec`, `Str`, `HashMap`, `Own`
  (for `unique_ptr`), and `Rc` (for `shared_ptr`).
  - `src/` names no STL container, `std::string`, smart pointer, or `std::to_string`.
    It no longer includes the `std_*.hpp` interop headers; `detail::text_of`,
    `dta_of`, `str_out`, and `decimal` replace them inside.
  - `std::stable_sort` (`sort_for_lookup`) became a bottom-up merge sort, because its
    buffer comes from `operator new`.
  - EASTL needs pointer ranges, not `std::span` iterators, without its
    std-iterator-category switch. Four call sites changed.
- **EASTL.** `MOIRA_USE_EASTL=ON` fetches EASTL 3.27.01 at a pinned commit with
  FetchContent. EASTL in turn fetches EABase. A host that already has an `EASTL` target
  uses its own. Neither is vendored; `THIRD_PARTY.md` lists them.
  - EASTL is private to the two libraries. Public headers are unchanged, so a host
    built either way sees the same API.
  - The STL and EASTL builds run the same assertions, with the same results (75,978 data,
    674 script, 11 allocator).
  - There is a CI job (`eastl`) and a local-matrix entry for it.
  - It is not built on MSVC in CI.
- **The test** is `moira_alloc_tests`. Its `main` installs a malloc default allocator
  before anything runs. In a separate file, because doctest's implementation brings in
  `<windows.h>`, whose `TextOut` macro renames `moira::TextOut`.
  - A Config pushes, reads, sets, snapshots, reloads, restores, and removes layers with a
    bound tweak. A Context registers a native, adds an object, defines a function and a
    class, runs `{new}` and `{delete}`, and fails a call.
  - In both, global `operator new` sees 0 allocations. The owner's allocator sees 14
    (Config) and 100 (Context), and is balanced after destruction.
- **Performance,** on this container, against `b60a411` built alongside. The hot paths
  allocate nothing, as before.

  Calls were measured at one sitting, three runs each, alternating builds:

  | Row | before | after |
  |---|---:|---:|
  | `{add 1 2}` | 31–32 ns | 31–35 ns |
  | `{+ 1 2}`, an overload set | 46–48 ns | 47–48 ns |
  | `{+ 1.5 2}`, promoted | 51–54 ns | 48–49 ns |
  | `{add $x 1}` | 35–38 ns | 35–36 ns |
  | `{add1 1}`, a script function | 70–72 ns | 71–74 ns |
  | `{bob grow 0}` | 53 ns | 53–56 ns |

  The lookup rows came from an earlier sitting:

  | Row | before | after |
  |---|---:|---:|
  | `find_float`, 20 children, avg | 10.6–12.1 ns | 11.5–13.4 ns |
  | `find_float`, last | 15–17 ns | 18–20 ns |
  | `try_find`, 50 children, linear | 14–15 ns | 19–21 ns |

  - **Cachegrind shows the same work per call:**
    - `{add 1 2}`: 360 instructions before and after;
    - `{add $x 1}`: 499 and 499;
    - `{add1 1}`: 991 and 990;
    - `{+ 1 2}`: 590 and 598;
    - `{+ 1.5 2}`: 769 and 774.
  - **One real regression was found and fixed.** With variable pages behind `Own<Page>`,
    GCC copied a cell's tag bytes with four byte stores instead of one. The load that
    followed stalled on store forwarding, and `{add $x 1}` lost 4–6 ns at the same
    instruction count. Pages are plain pointers again, and the code matches the old
    code.
  - **MSVC's CI found a second regression.** The overload-set path held its spill
    buffer in a `Vec<Node>`. MSVC's debug library allocates even for an empty
    `std::vector`, so every overload-set call allocated through the default allocator,
    and two zero-allocation tests failed. It is a stack array again, with a guard for the
    rare call of more than eight arguments. `NodeBuffer` would also have worked, but it
    cost 70 instructions a call. A `Vec` constructed on a hot path is a finding for this
    reason, even when it stays empty.
  - **The lookup rows are layout, not work.** `try_find` is inline in the benchmark's own
    file and unchanged. A separate driver linked against each library measures linear
    `try_find` on 50 children at 20 ns for both, with equal instruction counts and the
    same spacing between child arrays. This is the same code-layout effect D59 and D60
    saw.
  - The §7 targets hold: `{add 1 2}` ≤ 40 ns, and `find_float` ≤ 25 ns on average.
- **Size.** The core is 7,792 lines, up 159 in step 3 against a plan of +200.
- **Found on the way: `TextOut` collides with Win32.** `<windows.h>` defines `TextOut` as
  `TextOutA`/`TextOutW`. A Windows host that includes it before a Moira header calls
  functions that do not exist in the library, and fails to link. Step 2 introduced the
  name. Renaming it is a public API change, so it waits for the owner.

## D63. No compatibility layers; `TextOut` becomes `TextSink`

*Decided by the project owner 2026-10-11.*

- **The rule.** Breaking changes are allowed and encouraged. Moira keeps no
  backward-compatibility layers, deprecated aliases, or unused legacy code paths. An API
  change updates every caller in the repository and deletes the old form. `CLAUDE.md`
  states the rule for every session.
  - Script-level `Status::Deprecated` stays: it lets script authors mark their own
    functions, and is not a compatibility layer in Moira's API.
  - A search of the code found no other compatibility layer to remove.
- **`TextOut` is renamed `TextSink`** with no alias, which settles D62's open question.
  `<windows.h>` defines `TextOut` as a macro, so a Windows host that included it before a
  Moira header failed to link. `TextSink` matches `NoticeSink`. Functions that return
  one keep their names (`str_out`, `string_out`).

## D64. M5b step 4 design: one refcount per load

*Recorded 2026-10-11, before step 4's code. Answers D56's open point (how a value that
outlives its load keeps its tree alive), choosing D56's named fallback.*

- **Where sharing comes from.** Trees are not owned by one load today. Three kinds of
  holder share subtrees across loads, and each must keep working:
  - **trees built from trees:** a merged config tree reuses subtrees of every layer, and
    a macro body read by one file is spliced into the arrays of every later file in the
    session;
  - **holders of values:** a Context's variables and function bodies, a tweak's bound
    values, a snapshot's layers, a source map's keys;
  - **the host:** a `Ref<Array>` from `read_file` or `Config::root()`, or a `Node` from
    `eval`.

  So "a tree has one owner" (D56) does not describe the data. What can be true is that
  each array has one *allocation owner*, the load it was read in, which frees it with
  everything else of that load.
- **The owner is found from the array.** An `Array` or `String` header keeps 32 bits
  where its refcount was: the offset back to the start of its block. A block starts with
  a pointer to its arena, and an arena starts with its refcount. So retaining an array
  retains its arena, one more dependent load than today, usually in a line the block
  shares. Headers stay 24 and 8 bytes.
- **References inside a load are not counted.** A node stored in an arena does not count
  its own arena, which would never reach zero otherwise. A node that points into another
  arena counts it once per arena, not once per node: the arena keeps a short list of the
  arenas it depends on, released when it is freed. Freeing an arena runs no node
  destructors; it releases its dependencies and frees its blocks.
- **A one-off array is an arena of one.** `Array::make`, `make_from`, and `make_copy`
  without an arena build a solo block: its header, the array, and a count. A solo array
  owns its children as today: each child counts its target, and freeing the block
  releases them. Script-built values, `set` values, merge results, and host-built arrays
  are solo. That is one allocation per array, the same as now.
  - **This deviates from D56,** which put arrays built after load into an owner's scratch
    arena. A scratch arena only grows unless something copies its live values out, which
    is a collector. Solo blocks cost what arrays cost today, and nothing measured asks
    for more. Revisit if call statistics (M6) show script-built arrays in a profile.
- **Loads use arenas.** `read_string` and `read_file` build the whole tree, includes and
  macro bodies too, in one arena. Blocks grow from 4 KB, doubling to 1 MB, and an array
  larger than half a block gets a block of its own. The arena comes from the session's
  allocator, which a Config or Context sets to its own, so the alloc test (D61) now
  covers trees.
- **What changes for a caller.**
  - `Array::ref_count()` reports the owner's count. For a solo array that is the array's
    own count, as before.
  - `Array::set` on an arena array records a dependency if the new node points into
    another arena. The node it replaces stays alive until the arena is freed, because
    arena nodes are not counted.
  - Holding any node of a load holds the whole load. A Context that keeps one function
    defined by a large `eval` keeps that text's tree. That is the price of not counting
    per array, and the reason D56's per-array frees go away.
- **What a TD gets.** A parse makes one allocation per block rather than one per array,
  and teardown frees blocks without visiting nodes, closing gap 5's cost. Node copies
  outside a tree still touch a counter, the load's rather than the array's. Threads that
  copy nodes of one load all touch one line, where today they touch many. Most reads
  (`find`, `get`, tweaks) copy no node.
- **Measured before and after:** `bench_parse` (MB/s), the allocation count of a parse,
  teardown of a large tree, and the call rows, where an array-valued node is copied.

## D65. M5b step 4 results: an arena per load

*Recorded 2026-10-11. Implements D64. M5b is done, and `docs/m5b-plan.md` is deleted.*

- **What was built.**
  - **Owners.** An `Array` or `String` header keeps a 32-bit offset to its block where its
    count was. A block starts with its owner, and the owner holds the count. Headers stay
    24 and 8 bytes. `ref_count()` reports the owner's count.
  - **Solo blocks.** Arrays and strings built outside a load are solo blocks, an owner of
    one, from the default allocator. They own their children as before.
  - **Arenas.** `read_string`, `read_file`, includes, macro bodies, and `#merge` results
    build one arena per top-level read, from `LoadSession::allocator`. A Config and a
    Context set that to their own allocator.
    - A node stored in an arena counts nothing if it points into the same arena. If it
      points elsewhere, the arena counts that owner once.
    - Freeing releases those owners and frees the blocks, without visiting a node.
    - Until the load is returned, the arena's own count changes without atomics, because
      no other thread can see it.
    - `Array::set` on an arena array counts a foreign node's owner atomically.
  - `bench_parse` reports allocations and the time to free the tree. `bench_call` gains
    two rows that copy an array-valued variable.
- **Found on the way.** A `#merge` inside a load made solo arrays that pointed into the
  arena being built. The arena then adopted them, so each counted the other and neither
  was ever freed. ASan's leak check caught it. `#merge` now builds in the load's arena,
  and a test checks that a load with `#merge` has a count of 1.
- **Tests:**
  - `test_arena.cpp`:
    - a 4,001-object parse makes 18 allocations, and freeing its tree makes none;
    - a node outlives its load and holds all of it;
    - a macro body from an earlier load keeps that load alive after the session drops it;
    - `set` counts a foreign arena once;
    - `#merge` makes no self-dependency;
    - host-built arrays keep solo counts.
  - `moira_alloc_tests` checks that 50 reloads of a changing file keep the Config's live
    bytes flat, and that everything is freed after.
  - The alloc test (D61) now covers trees, which come from the owner's allocator: 19
    allocations for the Config scenario and 110 for the Context.
- **Performance,** against `d7feb82` built alongside, with the same benchmark files:

  | Row | before | after |
  |---|---:|---:|
  | parse, 4.2 MB | 110–122 MB/s | 127–129 MB/s |
  | allocations in that parse | 248,393 | 40 |
  | freeing that tree | 9.1–9.7 ms | < 0.01 ms |
  | `$loaded` (copy an array from a load) | 16.5–16.7 ns | 16.0–17.6 ns |
  | `$solo` (copy a host-built array) | 16.1–17.4 ns | 17.2–19.4 ns |
  | `{add 1 2}` | 27.6–31.4 ns | 26.8–28.8 ns |
  | `{add1 1}` | 66.5–69.6 ns | 64.9–65.3 ns |

  - The other call rows are unchanged within noise.
  - Copying an array-valued node now loads its block's owner pointer before the count.
    At most that costs about 1 ns, on the solo row.
  - Parse gained less than the allocation count suggests: lexing dominates, and glibc's
    malloc was fast for these small blocks. The memory a TD cares about, a few large
    blocks per load from the owner's allocator, is the real change.
- **Not done, by decision (D64):** arrays built after a load are solo, not in a scratch
  arena. The binary cache (M8) can load a whole tree into one arena block.
- **Size.** The core is 8,059 lines, up 267 in step 4 against a plan of +250. M5b as a
  whole is +797 against a plan of ~+450. Steps 2 and 3 added small types and the alias
  layer that the plan did not count (D60, D62).

## D66. M6 results: typed messages, call statistics, and the trace hook

*Recorded 2026-10-11. Implements spec §5's typed messages and debugging aids, with D53's
answers. The call stack was step 1 (D52). M6 is done, and `docs/m6-plan.md` is deleted.*

- **Typed messages.**
  - **What a message is.** An aggregate with `static constexpr auto name`.
    - Its fields are counted by a brace-initialization probe, at most 8; a ninth is a
      compile error.
    - They are read by structured binding.
    - Its identity is the address of `detail::message_key<M>`, an inline variable: no
      RTTI and no string compare. Across a DLL boundary on Windows each module would have
      its own address, so a host that sends messages across DLLs must define them in one.
  - **`b.on<M>(handler)`** records a typed entry and binds a method named `M::name` whose
    parameters are `M`'s fields, so `{bob hit 20}`, `check`, and the reference see an
    ordinary method. The reference marks it with the message type.
  - **`obj.handle(msg)`** returns `Expected<Node>` and follows D50's order:
    1. a script handler for the name, ruled out with one load from a per-Context bitmap of
       the names `{class}` handles;
    2. the typed entry, found by binary search over a few keys and called through one
       pointer, with no `Node` built;
    3. a method of that name, through the Context.

    Otherwise it returns a `NotFound` error naming the type and message. `handles<M>()`
    allocates nothing.
  - **Inheritance.** Entries are copied when a type's table is built, and a derived type's
    entry replaces its base's, for the C++ path and the script face alike.
  - **`handle_all(span, msg)`** looks the handler up once for each run of one type,
    without sorting or allocating. A span sorted by type pays one search per type, which
    is D53's "grouped by type" without a buffer. It skips objects that do not handle the
    message, returns how many did, and stops at the first error.
- **Call statistics.**
  - **What is counted.** Each function and each `{class}` handler keeps `calls`,
    `total_ns`, and `self_ns` in place. Native methods are not counted (their tables are
    shared between Contexts, D49).
  - **When.** Outside shipping, calls are always counted: one flag test and one increment
    per call, 4 instructions by cachegrind.
  - **Timing** waits for `set_profiling(true)` and reads a `TimeSource`, now in
    `<moira/clock.hpp>`. Self time subtracts what callees took.
  - **Reading.** `stats()` sorts by self time, then calls. `reset_stats()` clears the
    counters, and `hardening_candidates(threshold)` lists the script entries at or above
    the threshold. Reading allocates; counting and timing do not, which a test checks.
- **Tracing.** `trace(true)` reports each call to a sink on entry (with depth) and on exit
  (with its result or error). The default sink prints indented lines to stderr:
  ```
  {twice {add 1 2}}  (demo.dta:1)
    {add 1 2}  (demo.dta:1)
    -> 3
    {add $a $a}  (<eval>:1)
    -> 6
  -> 6
  ```
- **Shipping.** Counting, timing, and tracing are removed by `if constexpr`. `stats()` and
  `hardening_candidates()` are empty, and the switches do nothing; the tests check this
  in the shipping job.
- **Performance,** on this container, against `b4a8166` (messages, before statistics):

  | Row | before | after |
  |---|---:|---:|
  | `{add 1 2}` | 27.7–29.6 ns | 26.3–27.7 ns |
  | `{add1 1}` | 62.0–67.8 ns | 62.4–66.1 ns |
  | `{add 1 2}` with profiling on | – | 95–101 ns |
  | `{add 1 2}` tracing to an empty sink | – | 34–36 ns |
  | `bob.handle(HitMsg{1})` (target 15 ns, 0 allocations) | – | 8.2–9.3 ns |
  | `handle_all`, 64 objects | – | 424–437 ns (6.7 ns each) |
  | `ctx.call(bob, "grow", 1.5f)`, for comparison | – | 142–148 ns, 2 allocations |

  Two `steady_clock` reads cost about 70 ns here, which is why D53 put timing behind a
  switch.
- **Done-when criteria (spec §10):**
  - the message benchmark meets §7;
  - an error four frames deep renders its stack (D52's tests);
  - statistics add no allocations and compile out under shipping.
- **Size.** The core is 8,439 of 10,000 lines. M6 after step 1 added about 380 against
  the plan's ~390 for the same parts.

## D67. M7: tasks, first answers

*M7 plan (`docs/m7-plan.md`) Q1–Q7, answered by the project owner 2026-10-12.*

- **Q1, time.** `Scheduler::tick(dt)` advances a `double` in units the host chooses. There
  is one timeline per Scheduler; a game with game time and UI time makes two.
- **Q2, script variables.** A script task captures by value, when it is made, every bound
  `$variable` its body names, except globals. Each step re-binds them, and the task's own
  `{set}`s persist in its copy. This closes the original's open problem ("variable
  preservation", Appendix A) without explicit `(preserve ...)`.
- **Q3, the sync point.**
  - Deferred to the end of the tick (or `ctx.sync()`): spawning (a task starts at the next
    tick), `{delete}` and `remove`, and `cancel`, except a task cancelling itself.
  - Not deferred: `{new}` and registration.
  - This narrows D58's list, which also named `{new}` and registration: deferring `{new}`
    would stop a script from using an object it just made.
- **Q4, tasks and their object.** A task made with `$this` bound captures it, and is
  cancelled when that object is removed, as in the original.
- **Q5, waits.** `{wait cond}`, `{wait_for t}`, and `{wait_tick}` work only as a
  `thread_task`'s own top-level commands. The evaluator does not become resumable.
- **Q6, `interp_task`.** The spec's form: `{scheduler interp_task $var from to body...}`
  steps `$var` from `from` to `to` over `to - from` of the Scheduler's units, so `$frame`
  counts one per unit. A step runs `body` each tick with the value at that time, and the
  last step runs at exactly `to`. A decreasing range counts down over `from - to` units.
- **Q7, pool defaults.** A 4 KB frame budget by default, so debug builds (MSVC's
  especially) fit without tuning, at four times the memory per live task. Chunks of 64
  blocks, and no cap on live tasks unless `max_tasks` sets one. The first step still
  measures real frames on GCC, Clang, and MSVC and records the numbers.

## D68. M7 results: tasks

*Recorded 2026-10-12. Implements spec §5's tasks with D58's pool and D67's answers. M7 is
done, and `docs/m7-plan.md` is deleted.*

- **The C++ side** is `<moira/task.hpp>`: `Task`, `Scheduler`, `TaskHandle`, and the
  awaitables `delay(t)`, `until(predicate)`, and `next_tick()`.
  - **Frames come from the Scheduler's pool, never the heap.** The pool hands out blocks
    of the frame budget (4 KB by default), in chunks of 64 taken from the Context's
    allocator.
  - **Two guards make that a compile error.** A coroutine must take a `Scheduler&` or a
    `Context&` among its parameters, which a member coroutine's object does not hide.
    The promise's only constructor and its only `operator new` both require one, because
    Clang 18 falls back to the global `operator new` past the latter alone.
  - **Refusals.** A frame over the budget, or a spawn past `max_tasks`, is a `Limit` error
    naming the task and both sizes. A task made for another Scheduler is refused.
- **The Scheduler.**
  - **Waiting.** `tick(dt)` advances a `double` in the host's units. `delay` waiters sit
    in a heap keyed by wake time, `until` waiters are polled, and `next_tick` waiters are
    listed. A wake time within 1e-9 (relative) of now counts as due, so ten ticks of 0.1
    reach 1.0.
  - **Order.** A tick's batch runs by (wake time, spawn order). It is checked for order
    before it is sorted, because a batch of next-tick waiters usually comes back in
    order; sorting every tick cost 9 ns per task.
  - **Deferral.** A spawned task starts at the next tick, and a cancel made inside a tick
    waits for its end (D67).
  - **The default Scheduler** is made with the Context and added as the object
    `scheduler`. `Scheduler` is an `Object<Scheduler>`, so a host's extra Schedulers
    answer the same commands under their own names.
- **The script side.** `delay_task`, `interp_task` (the spec's form), and `thread_task`,
  with `{wait cond}`, `{wait_for t}`, and `{wait_tick}` as a thread's own commands. Each
  script task is a coroutine from the same pool.
  - **Captures.** Up to 16 bound variables are captured by value, kept in the frame, so
    making a task allocates nothing. Globals are left out.
  - **Steps** rebind the captures and keep the task's own `{set}`s. A task made with
    `$this` ends with that object.
  - **Errors** go to the notice sink with `{thread_task}` (or the kind) on the stack.
  - **Misplaced waits.** `{wait}` anywhere else is a `Refused` error.
- **The sync point (D67).** `{delete}` and `ctx.remove()` during any Scheduler's tick wait
  for the end of the tick. The outermost tick applies them, and `ctx.sync()` applies them
  on demand. Later tasks in the batch still reach the object. Destroying an object still
  removes it at once, because its memory is going away.
- **Deviation from the plan:** no per-body scan cache. Scanning a body for `$variables`
  takes part of the 230 ns that making a script task costs, and a cache keyed by array
  address would have to hold each array alive to stay correct. Revisit if call
  statistics show it.
- **Found on the way.**
  - `spawn` built its error prefix (`"task 'name'"`) on every call, which allocated for
    any named task. The script allocation test caught it; the prefix is now built only on
    error paths, and the C++ test now spawns named tasks.
  - A test meant to overflow the budget used an 8 KB local array that Clang's optimizer
    removed from the frame. The array is now written before a suspend and read after it.
- **Frames measured** (`SchedulerStats::largest_frame`), on GCC 13 release and debug and
  on Clang 18 debug alike:
  - a C++ task with a counter loop: 64 bytes;
  - a script task: 768 bytes, mostly its 16 capture slots.

  The 4 KB default leaves room for larger C++ tasks and for debug builds. MSVC's sizes are
  not measured: CI could not run (see below).
- **Performance,** on this container (`bench/bench_tasks.cpp`, new):

  | Row | Result | Target |
  |---|---:|---:|
  | `tick` with 10,000 tasks asleep, none due | 5–7 ns | < 100 ns |
  | resuming a task on `next_tick`, per task | 12.5–13 ns | ≤ 20 ns |
  | spawn and run to completion, per task | 26 ns, 0 allocations | 0 allocations |
  | a `thread_task` step: `{add 1 2}`, then `{wait_tick}` | 66–76 ns | – |
  | `{add 1 2}` evaluated directly, for comparison | 30–31 ns | – |
  | making a `delay_task` that captures two variables | 230 ns | – |

  The call rows (`{add 1 2}`, `{add1 1}`, `{bob grow 0}`, `handle(HitMsg)`) match `main`
  within noise, so making the default Scheduler with every Context costs calls nothing.
- **Tests:**
  - `test_tasks.cpp` (C++) and `test_script_tasks.cpp` (script) cover timing, order,
    locals and captures across suspends, cancellation, the budget, `max_tasks`, `$this`,
    waits, errors, the sync point, and zero allocations once warm;
  - `moira_alloc_tests` runs C++ and script tasks on a Context with a counting allocator:
    no global `operator new`, and 154 allocations through its allocator, all freed.
- **CI.** From the M7a push on, every CI job failed within seconds without a runner
  (`runner_id` 0, no logs). That is an Actions account or service problem, not a build
  failure. MSVC, libc++, TSan, and the fuzzers have not run on M7. The local matrix
  (debug, release, C++23, shipping, ASan+UBSan, Clang, data-only, plain refcounts, EASTL)
  passed before every commit.
  - *Update, 2026-10-12:* the cause was the repository being private. Once it was made
    public, CI ran again. MSVC reported one warning, an unused pack parameter in
    `task_home` (C4100), fatal under `-WX`; `[[maybe_unused]]` fixed it (`6064270`). All
    16 jobs then passed.
- **Size.** The core is 9,126 of 10,000 lines, up 687 in M7 against the plan's ~530. The
  script side and its tests of the sync point took the difference. That leaves 874 lines
  for M8 (~400) and M10's core instrumentation; M8's plan must say what M10 gives up if it
  overruns, or ask to raise the budget.

## D69. The line budget is 15,000

*Deviates from spec §0 again; supersedes D48's number. Decided by the project owner
2026-10-12, after M7.*

M7 brought the core to 9,126 lines (D68), leaving 874 of 10,000 for M8 (~400) and M10's
core instrumentation. The soft budget for the core is now 15,000 non-test, non-comment
lines. The spec and `tools/count_lines.py` say so. The rest of D2, D40, and D48 stands:
- the budget is reported, not enforced;
- each milestone records its count;
- disproportionate growth is still called out: M7 ran 30% over its estimate;
- M10's fallback (move the repetition report into a separate tool) applies at the new
  number.

## D70. M8: the binary cache, answers

*M8 plan (`docs/m8-plan.md`) Q1–Q4, answered by the project owner 2026-10-12, each as
recommended.*

- **Q1, D57's layout work.** Measure first.
  - Cold-cache rows (a working set larger than L2) come before any layout change.
  - The cache lays children beside their parent, which may cure gap 2b by itself; the
    tag run is built only if the cold rows still show child loads dominating.
  - The dispatch split (hot and cold records, the thunk in the table) is M8's last step,
    measured cold and warm.

  This narrows D57, which committed to all three.
- **Q2, staleness.**
  - What it checks: the header records every file the load read with a content hash,
    and the session's defines.
  - Who checks: `cache_fresh` and `read_cached`. Shipping trusts the cache.
- **Q3, writers.** A `moira_bake` tool (in `tools/`, outside the core) for build steps, and
  `read_cached`, which rewrites a stale cache in development.
- **Q4, Config.** `ConfigOptions::cache_dir`: outside edit mode, file layers load through
  `read_cached`, and reload rewrites the caches of changed files. Edit mode reads text.

## D71. M8.1: the cache writer and the in-place load

*Results of M8.1 (`docs/m8-plan.md` §1–2).* `include/moira/cache.hpp`: `write_cache` (to a
path or a `TextSink`), `read_cache` (a path, through a session), and `read_cache_bytes`.

- **The file is the arena block.** `Arena::create_holding` makes the arena, a block
  header, and the file one allocation. Each array's and string's block offset is written
  as `kBlockHeader` plus its offset in the file, so the loaded tree has the owner model of
  a parsed one (D64): one count, freed as one block, and `Array::set` on it adds
  dependencies as usual.
- **Layout.** A 72-byte header, then the arrays breadth first from the root (siblings
  together, so a lookup's child loads are adjacent), then the strings, then the symbol
  section: an 8-byte slot per symbol and the names. Arrays and strings the tree shares
  (macro expansions, D1) are written once and stay shared. The `aux` bits are written as
  zero.
- **Load.** Check the header, hash the file, intern each name into its slot, then one
  linear pass over the arrays: a node's offset becomes a pointer, and a symbol index (a
  symbol, a variable, an array's file) the slot's Symbol. Strings need no fixup. The
  slots live in the file, so the fixup pass needs no table: **one allocation**, against
  the spec's two (§7 counted one for the fixup pass).
- **Checks, and a deviation.** The pass checks every offset against its region, every
  symbol index against the count, and every node type. With the hash (a four-lane
  multiply-rotate checksum, about 8 GB/s), a damaged, truncated, or foreign file is an
  error. But a cache is build output, not input: a forged file with a valid hash can still
  point a node into the middle of another array. The parser's promise that nothing in a
  file can crash the reader holds for text, which comes from untrusted sources; the
  header comment says so.
- **Not in a cache:** the session's `#autorun` commands, which are not part of the tree.
  M8.2 decides whether `read_cached` keeps them.
- **An arena allocates nothing beyond itself.** Its list of dependencies was a `Vec`,
  which MSVC's debug library allocates even when empty (D62; CI caught the second
  allocation). It is now a raw array grown from the arena's allocator on the first
  dependency, so every load, text or cache, saves that allocation there.
- **Allocations on the path.** `read_cache(path, session)` opens the file from a stack
  copy of the path (a heap copy only beyond 511 bytes) and reads it unbuffered straight
  into the arena. A custom reader costs a temporary copy.
- **Tests.** Every corpus file, and every input the fuzz harness parses, loads from a
  cache structurally equal to its text load, with the same files, lines, heights, sorted
  flags, and sharing; damaged files fail with errors; the load is one allocation and the
  teardown none.

**Benchmarks** (`bench_cache`, release, this machine, best of 10; the generated text of
`bench_parse`, whose tree is about 4.3 bytes per byte of text):

| | 1.2 MB text, 5.3 MB tree | 4.2 MB text, 18.0 MB tree |
|---|---:|---:|
| text parse | 10.3 ms | 34.0 ms |
| `read_cache` from a file, warm symbols | **1.90 ms, 1 allocation** | 7.58 ms, 1 allocation |
| `read_cache_bytes`, warm symbols | 1.73 ms | 6.94 ms |
| `read_cache`, cold symbols (a fresh process) | 6.56 ms | 24.2 ms |
| freeing the tree | 0.001 ms | 0.001 ms |
| `write_cache` | 24.2 ms | 138 ms |

- §7's target, a 5 MB tree in ≤ 10 ms with ≤ 2 allocations, is met warm (1.9 ms) and in a
  fresh process (6.6 ms, which includes the process's first touch of its heap and the
  symbol table's growth).
- Of the 18 MB load, hashing is 2.6 ms and a copy of the file 1.8 ms (measured alone).
- `write_cache` is offline work (a hash map per array, string, and symbol) and not
  timed against a target; it matters for M8.2's `read_cached`, which writes after a parse.
- `bench_parse` and `bench_lookup` against the previous commit: unchanged within noise
  (parse 110–117 MB/s before, 114–119 after; lookup rows within ±1.5 ns).

## D72. M8.2: a cache load replays the session

*Answered by the project owner 2026-10-12, as recommended.* A text load can change its
`LoadSession` as well as return a tree: its `#define`, `#undef`, and macros stay for later
files, and its `#autorun` commands are collected. The cache records those effects
(defines and macros added or removed, and the autorun commands) and loading it applies
them, so a cache load leaves the session as the text load did.

## D73. M8.2: staleness, replay, `read_cached`, the `Cipher`, `moira_bake`

*Results of M8.2 (`docs/m8-plan.md` §3–4, D70, D72).*

- **Recording a load.** `LoadSession::record_for_cache`, when set, makes each top-level
  read record every file it read (path, size, content hash), every include candidate it
  looked for and missed, the defines and macros it began with, and where its `#autorun`
  commands start. Unset (the default), a read only clears two empty tables.
- **The record is an array in the blob.** `write_cache(root, path, &session)` builds one
  more array, `meta` (format in `cache.cpp`): `(files ...)`, `(before ...)`, and the
  load's effects, `(define ...)`, `(undefine ...)`, `(autorun ...)`. It is written beside
  the tree, so the writer, the fixup pass, and the checks needed nothing new, and a
  macro body the tree shares is written once. The header's four section fields became
  one offset; the header is 64 bytes.
- **`cache_fresh(cache, session)`** loads the cache and re-reads every recorded file
  through the session's reader: a changed file, or a missed one that now exists, makes it
  stale, as does any difference in the session's defines or macro bodies (compared by the
  hash of each body's text). A cache written without a recorded load is never fresh.
- **Replay (D72).** `read_cache` and `read_cache_bytes` apply the recorded effects to
  their session. A replayed macro body is an array of the cache's block, so the session
  keeps that block alive, as it keeps a text load's arena.
- **`read_cached(text, cache, session)`.** In development: the cache if it is fresh,
  otherwise the text, recorded, and a new cache written; failing to write it is not an
  error. Under `MOIRA_SHIPPING`: the cache if it loads, else the text, and nothing is
  written. Reads go through the session's reader, but caches are written with `fopen`:
  a host with its own file system writes through `write_cache(root, TextSink, ...)`.
- **`Cipher`** (spec §9): two functions over the bytes after the header, in place, and a
  user pointer, set as `LoadSession::cipher`. The hash covers the enciphered bytes, so
  damage is found before deciphering; an enciphered cache read without a cipher is an
  error. No implementation ships; the header keeps the anecdote.
- **`moira_bake [-D NAME]... [-I DIR]... IN.dta OUT.datb`** (`tools/`, outside the core)
  writes a recorded cache; a ctest bakes a corpus file.
- **Not recorded:** the session's include directories and reader. A different directory
  list that finds a different file is caught only through the missed candidates the load
  recorded.

**Benchmarks** (`bench_cache`, release, this machine):

| | 1.2 MB text, 5.3 MB tree | 4.2 MB text, 18.0 MB tree |
|---|---:|---:|
| `read_cached`, fresh cache (load, re-hash the text) | **2.15 ms** | 8.46 ms |
| `cache_fresh` alone | 2.22 ms | 9.05 ms |
| `read_cached`, no cache (parse, record, write) | 41.6 ms | 164 ms |
| text parse alone | 9.6–12.5 ms | 33–38 ms |
| `read_cache`, cold symbols, three fresh processes | 5.5–5.8 ms | 19–27 ms |

- The cold row was one process per run, and one run in M8.2's first measurement read
  18 ms where the previous commit read 5.9; repeated, both read 5.8–6.1 ms. The bench now
  runs three processes.
- `write_cache` dominates a miss (about 26 ms of 41 for the 5 MB tree): hash maps of
  every array, string, and symbol. It runs once per changed file in development.
- `bench_parse` against the previous commit: 119–126 MB/s before and after (noise).

## D74. M8.3: Config's `cache_dir`

*Results of M8.3 (`docs/m8-plan.md` §5, D70 Q4).*

- **`ConfigOptions::cache_dir`** (copied by the Config): outside edit mode, each file
  layer loads through `<cache_dir>/<hash of its path, hex>.datb`. A fresh cache gives the
  tree and replays the layer's defines on the session the layers above see; otherwise the
  text is parsed, recorded, and a new cache written through the Config's writer
  (`set_writer`), so a host's own file system gets it too. A cache that cannot be written
  is not an error. Under `MOIRA_SHIPPING` a layer trusts its cache, and writes none.
- **Reload needed no new code.** The layer's reader still records every file read, now
  except the cache itself: on a fresh cache, the freshness check's re-reads are the
  layer's dependencies; on a miss, the parse's. A changed file makes `reload()` re-read
  the layer through `read_cached`'s path, which finds the cache stale and rewrites it.
  The record lists the main file first (it was last), so a layer's `file` names it either
  way.
- **The directory must exist;** the Config does not create it.
- **Shipping gets its caches from a development build's Config:** a build step pushes the
  same layers, with the same paths and `cache_dir`, and the directory ships. `moira_bake`
  would bake each file alone, without the macros the layers below give it.
- **Edit mode reads text,** as before: write-back needs source maps.
- The freshness check behind a fresh cache moved into `detail::read_fresh_cache`, which
  `read_cached` and Config share, so neither loads a cache twice.

**Benchmarks** (`bench_cache`, release, `push_layer` of the generated file into a new
Config, best of 5):

| | 1.2 MB text, 5.3 MB tree | 4.2 MB text, 18.0 MB tree |
|---|---:|---:|
| layer from text | 30.2 ms | 492 ms |
| layer, `cache_dir`, fresh cache | **24.0 ms** | 329 ms |
| layer, `cache_dir`, no cache (parse, write, writer) | 82.8 ms | 690 ms |

- **What these show is Config's merge, not the cache.** The cache takes the parse out
  (about 10 ms of 30, and 160 ms of 490), but merging the layer into the Config dominates,
  and on this flat tree (11,000 and 37,000 top-level keys) it grows faster than linearly:
  about 22 ms, then about 320 ms, for 3.4 times the keys. That predates M8, and a game's
  config is rarely one flat table of 37,000 keys, but it is the next cost on this path.
  It is recorded here and goes to the owner rather than into M8.

## D75. Merge indexes large layers

*Answered by the project owner 2026-10-12 ("fix now", as recommended), after D74 found
Config's merge quadratic on flat layers; results here.*

- `merge_into` found each incoming key by scanning the existing ones: O(n²) for a flat
  layer of n keys. Past 32 incoming keys it now builds a table from each tag to its first
  existing child, once per call, and adds new keys as it appends them. Smaller merges, which
  is nearly every array of a real config, scan as before. The table is made only when
  used (an empty table allocates under MSVC's debug library, D62).
- A test merges 300 keys, duplicate tags included, at once (indexed) and 20 at a time
  (scanning), under both policies, and compares.

**Benchmarks** (`bench_cache`'s Config rows against the previous commit, release, best of 5):

| | before | after |
|---|---:|---:|
| 5.3 MB tree: layer from text | 30.4 ms | **13.1 ms** |
| 5.3 MB tree: layer, fresh cache | 24.4 ms | **6.7 ms** |
| 5.3 MB tree: layer, no cache (parse, write) | 101 ms | 55 ms |
| 18 MB tree: layer from text | 450 ms | **45 ms** |
| 18 MB tree: layer, fresh cache | 361 ms | 50 ms |
| 18 MB tree: layer, no cache | 683 ms | 275 ms |

- On the 18 MB tree a fresh cache (50 ms) is no faster than text (45 ms): its freshness
  check re-reads and hashes the 4.2 MB of text, and its 37,000-key merge into a table
  costs more than the cache saves. Shipping skips the check.
- `bench_parse`: 126 MB/s before and after (`#merge` takes the same path).

## D76. M8.4, step 1: cold lookup rows

*D57's measurement, as D70 (Q1) ordered it: cold rows before any layout change.*

`bench_lookup` gains cold rows: 50,000 objects of 20 tagged children (75.7 MB of tree),
visited in a shuffled order with a random key, from a text-loaded tree and from a
cache-loaded one (release, this machine, best of 7 runs of 2M calls):

| | warm (one object) | cold, text-loaded | cold, cache-loaded |
|---|---:|---:|---:|
| `find_float`, avg | 14.2 ns | 298–305 ns | 263–282 ns |

- **The cache's layout cures little:** 5–12% cold. A parse already allocates each array's
  children next to one another in its arena, so breadth-first order changes little.
- **Child loads dominate.** In a throwaway experiment over the same objects, reading only
  the parent's 20 nodes cost 56–81 ns, and the parent plus the one child a key names,
  23–25 ns (independent iterations overlap). The rest of `find_float`'s 260–300 ns is the
  linear scan loading each child's first node, one cache line per probe, to compare tags.
- **So the tag run is built (D70's condition holds):** each array keeps its children's
  tags beside its nodes, and the scan reads 4-byte keys from the parent's own memory. Its
  design note and results are the next entry.

## D77. M8.4, step 2: the tag run

*D70 (Q1) said to build it if the cold rows showed child loads dominating; D76 showed
they do. The owner chose its rule for re-tagged children, 2026-10-12, as recommended:
the same rule as sorted mode.*

- **Layout.** Each array is its 24-byte header, then a run of one 32-bit key per node
  (padded to 8), then its nodes. A key is 32 bits of the tag's Symbol pointer
  (`(p >> 3) | 1`), so making one loads nothing; 0 means the node is not an array with
  a symbol first. `try_find` scans the run and loads only the child a key matches, then
  confirms the child's tag, since two symbols can share a key. Misses are exact.
- **The run before the nodes, not after.** It was first placed after the nodes. That cut
  instructions and simulated misses but made the text-loaded cold row *slower* (325–393
  ns against 232–307): a lookup is a chain of dependent misses (header for `size_`, then
  the run, the node, the child), and a run after the nodes adds a link. Placed after the
  header, the run's address needs no load and shares the header's line. The price is
  that a node's address now depends on `size_`.
- **Keys from the Symbol's index, first tried,** cost a load from the symbol table per
  lookup: the 4-segment Path row went from 13.6 ns to 20 ns. Pointer-derived keys
  removed it.
- **A four-at-a-time scan, also tried,** halved the 200- and 1000-child scans but cost
  small arrays 3 ns and the cold rows 50 ns; the simple loop stays.
- **Kept by** `finish()` (every way of building an array: `make`, `make_from`,
  `make_copy`, a load's arena), `set()`, and `sort_for_lookup()`. A cache stores the run
  as its own symbol indexes, which the fixup pass turns into keys.
- **The rule (D77's question).** A child's tag is captured when it is placed. After a child
  is re-tagged in place, its parent's lookups may find it under neither tag until the
  parent sets it again. A parse, a merge, and a cache never re-tag in place; a test pins
  the rule.

**Benchmarks** (release, this machine, against the previous commit, two runs each):

| | before | after |
|---|---:|---:|
| `try_find`, 20 children, avg | 11.2–11.8 ns | **6.6–6.7 ns** |
| `find_float`, avg / last | 13.6 / 22.2–23.1 ns | **9.2–9.4 / 12.1–12.3 ns** |
| `find<float>`, avg | 13.0–13.2 ns | 10.7–11.1 ns |
| `find_float`, cold, text-loaded | 224–293 ns | **149–187 ns** |
| `find_float`, cold, cache-loaded | 262–280 ns | **138–154 ns** |
| `try_find`, 1000 children, linear | 367–392 ns | 292–313 ns |
| `get<int>(Path)`, 4 segments | 18.4–19.0 ns | 20.5–21.7 ns |
| `try_find`, 1000 children, sorted | 58–61 ns | 70–76 ns |
| tree size, 1.2 MB of text | 5.3 MB | 6.2 MB (+17%) |
| `read_cache`, that tree | 1.89 ms | 2.16–2.29 ms |

- **Where it costs, counted** with cachegrind in a small driver: a 4-segment path takes
  325 instructions (253 before) and a 1000-child sorted search 399 (360): computing a
  node's address from `size_` on each access, and a binary search is a chain of such
  accesses. Cold lookups take 140 instructions and 6.4 L1 read misses, against 183 and
  14.8.
- Parse speed is unchanged (123–125 MB/s), with 43 allocations instead of 40 (larger
  blocks fill sooner). `bench_call` and the Config rows are unchanged within noise.

## D78. M8.4, step 3: the dispatch split

*D57's last item, narrowed by D70 (Q1) to what measurement supports.*

- **Cold rows first.** `bench_call` gains two: one of 20,000 native functions, and one of
  2,000 named objects, each called in a shuffled order.
- **`Function`'s hot fields come first:** its name, then the single overload's thunk,
  callable, and arity, the overload count, and the call counters, about one cache line.
  The docs, status, overload list, and source location follow. A call with one overload
  reads only these; `refresh()` copies them from the overload list after each change (a
  script `{func}` and `add_overload`). An overload set still goes through the list.
- **Not in the name table, as D57 proposed.** Entries there would be indexed by every
  Symbol in the process (seven times the table's memory, and every Context pays it), and
  the counters a running call points at would move when the table grew. In the record,
  they cost the one load of the record, which a call made anyway.
- **Methods are not split.** The cold object row stays within about 22 ns of the warm one,
  and finding the object, not the method table, is most of it. Nothing measured asks for
  it; it waits for a workload that does.

**Measurements** (release, this machine). A small driver making 200,000 cold calls of
`{fN 1 2}`, counted with cachegrind, per call:

| | instructions | D1 misses |
|---|---:|---:|
| before the tag run (`675eb12`) | 323 | 8.00 |
| the tag run (D77, `e4b641e`) | 328 | 8.25 |
| the split (this step) | 331 | 6.74 |

Timed in the same driver, three runs each (best of 15 per run): 103–110 ns before the
tag run, 112–129 ns with it, 100–127 ns with the split. `bench_call`, against the tag
run's commit: cold natives 118–130 ns before, 103–117 ns after; cold objects and every
warm row unchanged within noise (`{add 1 2}` 31.0–32.9 ns).

- **A correction to D77:** the tag run costs a cold script call about 10 ns (larger
  command arrays, and a node address that waits on `size_`). D77's `bench_call` rows were
  all warm; this step's cold rows found it, and the split recovers most of it.

## D79. M8 results

*M8 (spec §10): binary cache: write, load, fixups, staleness detection, `Cipher` hook.
Done when the cache-load benchmark meets §7 and cache and text loads produce
structurally equal trees.*

- **§7, a 5 MB tree in ≤ 10 ms with ≤ 2 allocations: met.** With the tag run (D77) the
  generated tree for 1.2 MB of text is 6.2 MB. It loads warm in 2.3–2.5 ms with one
  allocation, and in a fresh process in 7.7 and 8.0 ms, with one run at 13.8 ms. That run
  was on a busier machine (parse read 110 MB/s, against 125 the same day). Scaled to
  5 MB, the fresh-process load is about 6–6.5 ms.
- **Structural equality: met.** Every corpus file and every input the fuzz harness parses
  round-trips through a cache with the same files, lines, heights, sorted flags, sharing,
  and tag runs (`test_cache.cpp`, `fuzz/parse_harness.cpp`).
- **Staleness, `Cipher`, and the rest.** Staleness is checked by content hashes and the
  defines (D73), a cache load replays its session effects (D72), and Config has
  `cache_dir` (D74). The `Cipher` hook is in, with no implementation shipped; the
  anecdote is in `cache.hpp`.
- **D57's layout work, measured first (D70):** cold rows for lookup and calls (D76, D78),
  the tag run (D77), and the dispatch split for functions (D78). Methods were not split:
  nothing measured asks for it.
- **Found on the way:** Config's merge was quadratic on flat layers (D75, fixed: an 18 MB
  layer from text in 45 ms, not 450), and the arena's empty `Vec` allocated under MSVC
  (D71).
- **The core** is 9,850 lines of the 15,000 budget (D69), against the plan's ~9,690.
  `moira_bake` is in `tools/`, outside the core.
- `docs/m8-plan.md` is deleted; its answers are D70 and D72.
- **Naming.** Commits called M8's steps M8a–M8d; this file calls them M8.1–M8.4, because
  the spec's milestone M8b is `moira::shape`, which is next.

## D80. M8b: shapes and packs, answers

*`docs/m8b-plan.md` Q1–Q4, answered by the project owner 2026-10-12, each as recommended.*

- **Q1, what a shape governs:** every array tagged with the shape's name, anywhere in the
  tree; `(at path)` in the declaration narrows it to arrays under a path.
- **Q2, "reported at load":** Config runs a validator after each merge (a callback
  moira::data defines and moira::shape provides), so pushes, reloads, and restores report
  violations, located, through the Config's warning sink. moira::data knows nothing of
  shapes.
- **Q3, the name:** what spec §1 calls a pack's *layer* is its **level**:
  `(pack name (level game))`, `session.set_pack_levels({"engine", "game", "task"})`. A
  Config's layers are its stack of files, and `Tier` already names a function's
  implementation. This deviates from the spec's wording.
- **Q4, the one-way rule's warnings:** macro uses, found while parsing, are warnings on
  the `LoadSession` (`session.warnings()`), which Config forwards to its warning sink;
  calls to a softer pack's function are a third list in `CheckReport`.

## D81. M8b, step 1: `#require`

*Results of `docs/m8b-plan.md` §1.*

- `#require path` resolves like `#include` and splices the file the first time a session
  sees it; afterwards, from any file and any later top-level read through that session,
  it is a no-op that does not read the file again. The set is the session's, copied with
  it. `#include` of a required file still splices it.
- Resolution stops at the first candidate path the session has required, before reading
  anything; otherwise the first candidate that exists is read and joins the set.
- **Caches (D72).** A recorded load's record gains `(required ...)`, the files required
  when it began, and `(require ...)`, the ones it required. A cache is stale for a session
  whose required set differs from the first (a `#require` would splice differently), and
  loading one adds the second to the session.
- **Tests:** a pack required from three included files is read once, and its macro serves
  all three (the plan's third criterion); later reads, copies, and fresh sessions; a
  missing file; a cache round trip and its staleness.
- `bench_parse`: 121–126 MB/s, unchanged. The core is 9,886 lines.

## D82. M8b, step 2: packs and the one-way rule for macros

*Results of `docs/m8b-plan.md` §2 (D80).*

- **Declaration.** A file whose first form is `(pack name (level L) (requires ...))` is a
  pack: the session records it (`packs()`, `pack_of(file)`; name, level, its rank among
  `set_pack_levels({...})`, file, requires) and the tree leaves the form out. Directives
  may come before it; a later `(pack ...)` is ordinary data. A pack without a level, or
  with one not in the session's list, is an error at load. Other children (`owners`,
  `for`, `stage`) are accepted and kept for M10.
- **Attribution.** Each macro and flag a pack's file defines is attributed to that pack
  (redefined outside a pack, it loses the attribution). Macros defined before the
  declaration belong to no pack.
- **The one-way rule, for macros.** Expanding, in a pack's file, a macro that a softer
  pack defined is a warning in `session.warnings()`, once per file and macro, with file
  and line. Softer using harder, and files that declare no pack, pass silently. Config
  forwards a layer's warnings to its warning handler.
- **Caches (D72).** A recorded load's record adds the session's pack levels (a cache is
  stale for other levels), the packs it declared, and the pack of each macro it defined;
  a cache load replays both, so later text loads still warn about a cached pack's macros.
- **Tests:** the plan's fourth criterion for macros (a game pack using a task pack's
  macro warns, once, located; task using engine does not), level errors, first-form only,
  cache replay and staleness, and Config's forwarding.
- `bench_parse`: 128–131 MB/s (121–126 before; noise). The core is 10,064 lines, about 70
  more than the plan's estimate for this part.

## D83. M8b, step 3: the one-way rule for calls

*Results of `docs/m8b-plan.md` §2, its last bullet (D80, Q4).*

- `check(tree, ctx, report, &session)` takes, optionally, the session that loaded the
  tree. With it, a call in a pack's file to a script function defined in a softer pack's
  file is a warning in the new `CheckReport::warnings`, once per pack and function, at the
  first call. The callee's file is its `{func}` in the checked tree, else the file the
  Context recorded when the function was defined. Native functions belong to no pack.
- `write(report)` prints warnings after errors, prefixed `warning: `.
- `script.hpp` includes `parse.hpp` for `LoadSession`.
- **Tests:** the plan's fourth criterion (a game-level pack calling a task-level function
  is warned, once, located; the task pack calling the engine pack is not), and the same
  for a function the Context already registered; nothing without the session or from a
  file that declares no pack.
- No hot path changes: `check` is a tool. The core is 10,095 lines.

## D84. M8b, step 4: shapes are declared in `(shapes ...)`

*Answered by the project owner 2026-10-12, as recommended; with the design note for
`moira::shape` (`docs/m8b-plan.md` §3, D80).*

- **The deviation.** Spec §6 writes `(shape boss_phase (field ...) ...)` at the top level.
  Config merges layers by tag, so every such form would be tagged `shape`, and a second
  layer's `(shape other ...)` would merge into the first's, its name overwriting
  `boss_phase` as a value. Shapes are declared instead in one container, each tagged by
  its own name:

  ```
  (shapes
    (boss_phase (at bosses * phases)
      (enter_at float (range 0 1) (kind percent) (desc "HP fraction that starts it"))
      (attacks  (list symbol))
      (cadence  float (range 0.2 5) (optional))))
  ```

  Layers merge it like any data: a later layer adds a shape, adds a field, or retunes one
  field's range.
- **The library.** `moira_shape` (`include/moira/shape.hpp`, `src/shape/`), namespace
  `moira::shape`, depends only on moira::data, built unless `MOIRA_BUILD_SHAPE=OFF`, and
  counted apart from the core against spec §6's 600 lines.
  - `Set`: the shapes read from trees' `(shapes ...)`; a later declaration of a field
    replaces an earlier one. Malformed declarations are located errors.
  - A field: a name, a type (`int`, `float`, `bool`, `symbol`, `string`, `any`, `(list T)`,
    or a shape's name), `range`, `step`, `kind`, `desc`, `note`, and `optional`.
  - **What a shape governs (D80 Q1):** every array tagged with its name; `(at ...)` narrows
    it to those whose ancestors' tags, from the root, are the given ones (`*` matches any).
  - `check(tree, set, report)`: a field of the wrong type or out of range, or a required
    field missing, is an error; a field the shape does not declare is a warning (spec §6).
    Commands and variables are not checked: their values are unknown until run. Each
    finding names the file, line, and path.
  - Enumeration: `set.shapes()`, each shape with its fields and their metadata.
- **"At load" (D80 Q2).** Config gains `set_validator(...)`, called after every merge
  with the merged root and the warning handler; `moira::shape::validator()` returns one
  that reads the root's `(shapes ...)` and checks the root against them.

## D85. M8b, step 4: `moira::shape`

*Results of `docs/m8b-plan.md` §3 (D80, D84).*

- **The library** is `moira_shape` (`include/moira/shape.hpp`, `src/shape/shape.cpp`):
  400 lines of spec §6's 600, outside the core, which `tools/count_lines.py` now reports
  separately. It links moira::data only; the data-only build builds it too.
- **What D84 described, built**, with two settlements:
  - A field declared again (by `Set::add` of another tree) keeps the metadata the new
    declaration does not restate, as Config's merge of two layers would.
  - `bool` is what `Convert<bool>` reads: numbers, not the symbol `TRUE` (DTA defines
    `TRUE` as a macro for 1).
- **Matching by tag (D80 Q1) does not reach keyed children.** In `(bosses (dragon ...)
  (golem ...))` the children are tagged by their names, so no shape governs them; the
  data must write `(boss (name dragon) ...)`. Recorded as a limitation, for the owner to
  revisit if real data asks.
- **Config.** `cfg.set_validator(moira::shape::validator())` checks every merge: each
  push, pop, toggle, reload, and restore reports errors and warnings, located, through the
  warning handler. The load itself never fails on a shape.
- **Tests** (`tests/shape/`): corpus-style boss data and a patch layer loaded through
  Config. An out-of-range value and a missing field are reported with file, line, and
  path; an extra field warns. The patch retunes a range and declares the field, and one
  error remains (the plan's first two criteria). Also types, lists, nested and listed
  shapes, commands and `any` passing, malformed declarations, unknown shapes, enumeration
  with metadata, and copies.
- **Cost**, measured in a small driver: checking 50,000 instances (3.2 MB of text)
  takes 6.5 ms. Shapes cost nothing unless a validator is set or `check` is called; the
  Config change is one null test per merge.

## D86. M8b results

*M8b (spec §10): `moira::shape` (shape declarations, shape-aware `check`, the
enumeration API) and `#require`.*

- **1. A shape violation in corpus-style data is reported at load, with file and line:
  met.** Config's validator (D85) reports an out-of-range value at `bosses.dta:23` and a
  missing field at `bosses.dta:30`, each with its path, on the push that loads the file.
- **2. Extra fields warn without failing: met.** The golem's `taunts` is a warning, and
  the layer loads.
- **3. A pack required from three files defines its macros once: met** (D81): it is read
  once, and its macro serves all three.
- **4. A game-level pack referencing a task-level function warns: met** (D83), as does
  one expanding a task-level macro (D82).
- **Deviations:** *level* for the spec's pack *layer* (D80), and `(shapes (name ...))`
  for the spec's `(shape name ...)` (D84). Matching by tag does not reach keyed children
  (D85).
- **Sizes:** the core is 10,103 lines (of 15,000; the plan estimated about 10,060), and
  `moira::shape` 400 (of 600, outside the core).
- `docs/m8b-plan.md` is deleted; its answers are D80 and D84.

## D87. M9: the docs and examples, answers

*`docs/m9-plan.md` Q1–Q4, answered by the project owner 2026-10-12, each as recommended.*

- **Q1, `harness-imgui`:** Dear ImGui fetched at a pinned release (as EASTL is), GLFW and
  OpenGL 3 for the window, behind `MOIRA_BUILD_HARNESS` (off by default). The panels are
  plain functions over a `Config`, `TweakRegistry`, and `Context`, so CI runs them
  headless: ImGui with no backend, a few frames, and assertions on what changed.
- **Q2, `remote-console`:** portable, with BSD sockets and a small Winsock shim, built and
  tested on every CI platform; the test connects over loopback, sends three commands, and
  checks the replies.
- **Q3, the README:** short. The pitch, the five-minute path to a working tweak, one
  paragraph per feature with links, the build table, and the measured numbers; the long
  feature list moves into `docs/language.md` and the cookbook.
- **Q4, doc tests:** the README's five-minute path is also a CTest program
  (`examples/quickstart`), and a script checks that the README's code block matches it
  line for line.

## D88. A tweak's value stops before its metadata

*Found writing `examples/harness-imgui` (M9); answered by the project owner 2026-10-12:
fix it in M9.*

- **The gap.** A tweak bound to every value after its tag, metadata included. A scalar
  read only the first, so `(width 2 (range 0 10))` worked, but a value of several
  numbers did not: `(tint 0.9 0.5 0.2 (range 0 1))` gave a `Tweak<std::array<float, 3>>`
  five values, which did not convert, so it warned and kept its default. The inference
  that a 3- or 4-number value ranged 0..1 is a color could never apply to a range written
  in the data.
- **The rule.** A tweak's value ends before the trailing arrays tagged with the keys a
  tweak reads as metadata: `range`, `step`, `desc`, `note`, `kind`, and `group`. Other
  tagged elements are the value's own, so a `Convert<T>` that reads tagged arrays reads
  as before. Write-back already took the untagged children as the value, so a live edit
  of such a tweak writes back its numbers and keeps the metadata.
- **Not changed:** `get<T>` on a path is data lookup, not a tweak, and still sees every
  value; `get<std::array<float, 3>>` of the tint above fails as before. Metadata in data is
  a tweak convention (spec §4), and a lookup cannot tell metadata from an element.
- **Cost:** none on a hot path. `TweakBase::bind` runs per tweak on each merge, and the
  check walks back over the value's trailing arrays only.
- **Tests:** a three-number tweak and a list with metadata in the data bind, infer
  `color`, and write back keeping the metadata; a trailing tagged element that is not
  metadata stays in the value. The harness's ball color keeps its range in the data.

## D89. M9 results

*M9 (spec §10): `README.md`, `docs/language.md`, `docs/cookbook.md`, and `examples/`,
with D87's answers. Done when a reader can go from zero to a working tweak in under five
minutes of reading, the cookbook's live-ops section covers environment boundaries,
version history, rollback, and personal and experiment layers, and the harness pushes a
personal `local` layer by the convention in spec §4.*

- **1. Zero to a working tweak in under five minutes of reading: met.** The README's
  path, from its first line to the edited value printing, is about 290 words of prose and
  40 lines of code and data: about a minute and a half of prose at 200 words a minute,
  and the code besides. `examples/quickstart` is that path as a program; its test copies
  the data, edits it, and presses Enter, and `tools/check_readme.py` fails if the
  README's blocks and the files differ by a line (D87 Q4).
- **2. The cookbook's live ops: met.** Environment boundaries (stacks by directory,
  `freeze()` and `live_layers`, shipping), version history (overlays as dated files under
  review, layer names as provenance, `snapshot()` beside a repro), rollback
  (`remove_layer`, and why it leaves no trace), tests before an overlay ships (shapes and
  `check`, in CI), and personal and experiment layers. `examples/live-ops` runs them in
  development and shipping builds alike.
- **3. The harness pushes `local/<user>.dta`: met.** `examples/harness-imgui` pushes it
  with `{.optional = true}`; "Keep for me" writes a value there, creating the file, and
  its headless test checks that.
- **What was built.**
  - `README.md`, short (D87 Q3): 203 lines, against 206 that stopped at M7.
  - `docs/language.md` (373 lines): data, the preprocessor, packs and levels, shapes,
    script, and errors, with D80, D84, D85, and D54 as built rather than as the spec
    wrote them. `docs/cookbook.md` (293 lines): its three sections, then tweaks in
    practice, reload callbacks, live ops, the cache in a build step, harness packs, a
    console over `eval`, and repro scripts.
  - Examples, each a CTest run against expected output: `quickstart`, `config`,
    `scripting`, `objects`, `tasks`, `live-ops`, and `remote-console`, beside
    `one_line_tweak` and `calculator`, which stay. The console is 89 lines of code over a
    52-line socket shim, tested over loopback on every CI platform, MSVC included (D87
    Q2).
  - `examples/harness-imgui` (D87 Q1): Dear ImGui v1.92.9b and GLFW 3.4 at pinned
    commits, behind `MOIRA_BUILD_HARNESS`; Tweaks, Shapes, Console, Loop, Layers, and
    Snapshots panels, provenance on hover, write-back. CI builds it on Linux and MSVC and
    runs its panels headless. The local matrix gains a harness configuration.
- **A gap the docs exposed, fixed (D88):** a tweak of several numbers could not carry
  metadata in the data. The owner chose to fix it in M9; it is the milestone's only core
  change.
- **Not changed:** no benchmark row; the core changed only in `TweakBase::bind`, which
  runs per tweak on each merge. The core is 10,116 lines of 15,000 (10,103 before; D88),
  and `moira::shape` 400 of 600.
- `docs/m9-plan.md` is deleted; its answers are D87.

## D90. M10: harness instrumentation, answers

*`docs/m10-plan.md` Q1–Q5, answered by the project owner 2026-10-12.*

- **Q1, loop statistics' samples (as recommended):** a fixed table allocated once, up to
  32 files and 16 layers, each with its last 32 edit-to-see and reload times (about 10 KB
  per Config); the least recently changed file gives up its row. Only reloads that changed
  something feed it, because a host polling four times a second fills M3b's 256-record
  history with `Unchanged` records in about a minute (found in M9's harness).
- **Q2, journal values (as recommended):** inline, up to four nodes each for the old and
  new values, with the true count; about 160 bytes an entry in a ring allocated once, so
  `set`'s fast path stays allocation-free.
- **Q3, the repetition report: in moira::data,** not in `check` as the spec puts it and
  the plan recommended. `moira::find_repeats(trees, options, out)` needs no Context, so
  data-only projects have it; `check` calls it and reports the result in `CheckReport`.
- **Q4, owner findings (as recommended):** packs that declare `(owners ...)` with fewer
  than two names. A pack without the form is not reported.
- **Q5, shipping (as recommended, on reconsideration):** the runtime instrumentation
  (`mark_visible`, the statistics, the journal) compiles out, as the spec says. `check`,
  `find_repeats`, `write_json`, and the reference stay in every build, as `check` and the
  reference do now: they cost nothing unless called, and CI can run them against shipping
  defines. The owner first chose to compile the tools out too, then reconsidered the same
  day.

## D91. M10, step 1: pack harness metadata and `origin`

*Results of `docs/m10-plan.md` §3 and §4 (D90).*

- **Pack metadata.** `(owners ...)` and `(for ...)` list names, as strings or symbols;
  `(stage S)` is `genesis`, `custom`, `product`, or `commodity`, and anything else is a
  located error at load. `Pack` gains `owners`, `audience` (the `for` form; `for` is a C++
  keyword), and `stage`, empty when not declared.
- **`origin`, a settlement of the spec's "as a top-level form".** Spec §1 lets
  `(origin "economy.xlsx" (tool sheets-export))` appear anywhere at the top level, but
  `origin` is an ordinary tag in existing data (a spawn point's `(origin 0 0 0)`).
  As with packs (D82), it is a declaration only as the file's first form, or the first
  after its pack declaration, and only when its first value is a string; one per file.
  Anything else is data. The parser records it in the session (`origins()`,
  `origin_of(file)`) and leaves it out of the tree. An included file declares its own.
- **`Source`** gains `origin` and `tool` (24 to 40 bytes), filled from the session the
  supplying layer was read with, by `source()`, `layer_effect`, and `diff`. Every member
  now has an initializer, so `{layer, file, line}` still builds one.
- **Caches (D72).** A recorded load records each pack's metadata and the origins its files
  declared, and a cache load replays them. The format version goes from 1 to 2, so an old
  cache is stale (`read_cached` and Config re-read the text).
- **Tests:** metadata and its errors; origins first, after a pack, in an included file,
  without a tool, and the data that is not taken for one; a cache round trip; `source()`
  and `layer_effect` naming the origin of a value from an included generated file.
- **Benchmarks** (release, against `9dbcd38`, alternating runs): `bench_parse` 131–136
  MB/s before and after. `bench_cache`'s text parse overlaps (94–125 MB/s before, 107–121
  after). Its `read_cache` row read 2.29–2.48 ms before and 2.43–2.84 after, though
  `read_cache` runs none of the changed code. Counted with cachegrind, five loads of the
  6.2 MB tree take 80.78 M instructions and 1.01 M D1 misses in both builds, so the
  difference is timing noise.
- **Size:** the core is 10,250 lines (10,116 before; the plan estimated 130 for this step).

## D92. M10, step 2: loop statistics and `mark_visible`

*Results of `docs/m10-plan.md` §1 (D90 Q1).*

- **`ReloadRecord`** gains `layer` (the layer of the first changed file) and `visible_ns`,
  120 to 136 bytes.
- **`cfg.mark_visible()`** is inline: one load and a branch on a flag a reload sets when it
  changed something. With something waiting, it reads the clock once, stamps `visible_ns`
  on those records (if the ring still holds them), and rewrites their edit-to-see samples
  to run to now. Up to 8 changed reloads wait between frames; more keep the sample their
  reload's end gave them.
- **Edit-to-see is sampled when the reload ends** (`end_ns - origin_ns`) and rewritten by
  `mark_visible`, so a host that never calls it still gets the substrate's share, and one
  that does gets the whole loop. A reload without an origin has a reload time but no
  edit-to-see. This settles the plan's "else nothing".
- **The table** (D90 Q1): 32 file rows and 16 layer rows, each with its last 32
  edit-to-see and reload times, kept as 32-bit microseconds (saturating at about 71
  minutes) to stay near Q1's 10 KB: 48 rows of 296 bytes, 14 KB, allocated by the first
  reload that changes a file. A name keeps its row; a new one takes a free row or the least
  recently fed one in its half. Only reloads that changed a file feed it, failed ones
  included; unchanged and refused reloads count in the totals only.
- **`cfg.loop_stats()`** (tool path, allocates its result): the totals (reloads,
  unchanged, failures), and per row the reloads and failures since it got its row, and
  the median (the upper middle of an even count) and worst of each window, in
  nanoseconds. Rows are most recently changed first.
- **Shipping:** reload is inert, so nothing feeds the table; `mark_visible()` does nothing
  and `loop_stats()` is empty.
- **Tests** (`tests/data/test_m10.cpp`): edit-to-see per file and per layer, to the
  reload's end and then to `mark_visible`; no clock read when nothing waits; 100 polls
  change nothing; median and worst over four samples; a reload without an origin; a parse
  failure counted against its file, layer, and the total; a 33rd file taking the least
  recent row; no allocation in `mark_visible`.
- **Benchmarks** (`bench_cache`, two new rows, release, three alternating runs against
  `5f0dfad` with the same benchmark file): an unchanged reload of a small file, 1,782–1,892
  ns before and 1,448–1,811 ns after (noise; the added counting is a few increments).
  `mark_visible` with nothing waiting, 0.25–0.32 ns a call, against 0.15 ns for the empty
  loop before. `bench_parse` 152 and 150 MB/s.
- **Size:** the core is 10,434 lines (10,250 before; the plan estimated 160).

## D93. M10, step 3: the edit journal

*Results of `docs/m10-plan.md` §2 (D90 Q2).*

- **`JournalEntry`:** the path (as `unsaved()` names it), a time from the Config's time
  source, the old and new values (up to four nodes each, inline, with their true counts;
  `before_count` 0 for a path that had no value), the old value's `Source`, the fate, and
  for `Written` the file and line. `cfg.journal(fn)` visits them oldest first;
  `set_journal_history(n)` (default 256, 0 off) allocates the ring once.
- **What feeds it:** every live change through the Config: `set` (its fast path too),
  `Tweak::set`, `set_values`, and so the console and the harness's panels.
- **Fates.** `Superseded` when the path is edited again before it is saved; `Written` when
  `write_back`, `write_back_to`, or `promote` from scratch writes it (with the line);
  `Reverted` when `clear_scratch` drops it, or when `restore` leaves it out of scratch
  (the plan named only `clear_scratch`; a restore drops edits just as surely).
- **Without searching.** A path has at most one unsaved entry, the one its scratch entry
  holds, so a scratch entry keeps that entry's sequence number, and every fate is found in
  one step: superseding on every `set` never scans the ring. Entry *n* of the session sits
  in slot *n* mod the capacity while the ring holds it. Scratch entries now keep their
  path as an interned `Symbol`, made once when the entry is, so the fast path interns
  nothing.
- **Size:** an entry is 216 bytes (the plan estimated 160): eight 16-byte nodes, a 40-byte
  `Source` (D91 added `origin` and `tool`), and the rest; 55 KB for 256, allocated by the
  first edit.
- **Cost on `set`'s fast path** (`bench_cache`'s new row, a scalar already in scratch,
  release, three alternating runs against `623c29f`): 17.8–23.7 ns before, 51–65 ns after.
  Counted with callgrind, the first version wrote each slot through a temporary entry
  (about 400 more instructions a `set`); writing fields in place brought it to about 280.
  Timed in a small driver, the journal itself is 5–10 ns of it, and reading the default
  `steady_clock` about 40 ns on this machine. Kept: a live edit comes at most once a frame
  per control, the path's contract is that it allocates nothing (still tested: 100 edits,
  no allocation), and a host can give the Config a cheaper time source, such as a frame
  counter.
- **Shipping:** `set` is refused, so the journal stays empty.
- **Tests** (`tests/data/test_m10.cpp`): a tweak's edit with its old value and source;
  superseding through the fast path; written by `write_back`, `write_back_to`, and
  `promote`; reverted by `clear_scratch` and by `restore`; a six-node value; the ring
  wrapping, turned off, and allocation-free on the fast path. M3b's tests pass unchanged.
- **Size:** the core is 10,560 lines (10,434 before; the plan estimated 150).

## D94. M10, step 4: the repetition report and owner findings

*Results of `docs/m10-plan.md` §5 and §6 (D90 Q3, Q4).*

- **`moira::find_repeats(trees, options)`** (`<moira/repeats.hpp>`, moira::data, as Q3
  chose) returns a `List<Repeat>`: one occurrence as a node (`form`), its size in nodes
  counted recursively, and every site (file, line). The plan wrote it with an out
  parameter; a returned `List` is how Moira hands back results elsewhere. Options:
  `min_nodes` (6) and `min_repeats` (3; 0 turns it off).
- **How.** Each array is hashed once (memoized by address) from its nodes' types and
  values and its children's hashes; arrays, commands, and properties are told apart.
  Large enough subtrees are grouped by hash and split into classes with
  `structurally_equal`. An array reached twice (a macro's expansion) counts once.
  **Only the largest:** classes are taken largest first, and one whose every occurrence
  lies inside an array already taken is not reported; one that also appears on its own is
  reported with all its sites. **Stable:** repeats are ordered by first site (file, then
  line), then size, then text, and sites by file and line, so nothing depends on memory
  addresses.
- **`check` changes shape, a breaking change by the rules.** Repetition must see every
  file at once, but `check` took one tree per call. It gains
  `check(std::span<const Array* const> trees, ctx, report, options)`, and the trailing
  `const LoadSession* packs` becomes `CheckOptions{.packs, .repeats}`, in every form.
  Within one call, every tree's `{func}` definitions are collected before any call is
  checked. Every caller in the repository is updated (`{.packs = &session}`).
- **`CheckReport`** gains `repeats` (appended per call) and `findings`. **Owner findings**
  (Q4): with the session, each pack whose `(owners ...)` lists one name is a finding at the
  pack's file, once per report. The text form prints `repeat:` and `finding:` lines after
  the demand report.
- **Tests:** M10's fourth criterion (a subtree in three files, all three locations);
  twice is not enough; a difference in one value; only the largest; a macro counted once;
  commands apart from arrays; the same order on a second read; the fifth criterion (a
  one-owner pack reported, once, without being an error).
- **Cost** (a tool path; release, a small driver): the 1.2 MB benchmark text (a 6.2 MB
  tree) in 28.7 ms, about three times its parse; the corpus in 0.05 ms. No runtime path
  changed.
- **Size:** the core is 10,772 lines (10,560 before; the plan estimated 230 for these
  two parts).

## D95. M10, step 5: machine-readable check output, and the harness register

*Results of `docs/m10-plan.md` §7, and the reference's part of §3 and §4 (D90).*

- **`write_json(report, sink, &session)`**, and `to_json(report, &session)` in
  `<moira/std_script.hpp>`: one object, `"schema": "moira.check/1"`, then `errors`,
  `warnings`, `demand`, `repeats`, `findings`, and `packs`. Documented field by field in
  `docs/check-json.md`, with the versioning rule: fields are added only with a new minor
  version, removed or renamed only with a new major.
- **Stable across runs** (M10's sixth criterion): problems sorted by file, line, then
  message; demand by name, then class; sites by file and line; repeats in
  `find_repeats`' order; packs by name. One list entry per line, so a milestone's output
  diffs cleanly against the last. Absent text is `""`, not `null`, so every field always
  has one type. Errors carry their `ErrorCode`'s name and their path.
- **The packs come from the session**, as the plan said, since a `CheckReport` does not
  hold them; without one, `packs` is an empty list.
- **The reference as harness register** (reading-a-studio §7): `reference(ctx, out,
  &session)` and `write_reference(ctx, path, &session)` add a Packs table (level,
  requires, owners, audience, stage, file) and an Origins table (tool, source, file).
  Without a session the reference is unchanged, so `docs/script-reference.md` is too.
- **Tests:** a check over two files whose report has something in every section, written
  twice and compared byte for byte, then against `tests/script/check_expected.json`
  (regenerated with `MOIRA_UPDATE_REFERENCE=1`, as the reference is); an empty report; the
  reference's Packs and Origins sections, and their absence without a session.
- **Cost:** tool paths only; nothing on a runtime path changed.
- **Size:** the core is 10,955 lines (10,772 before; the plan estimated 230 for the JSON
  writer and the reference's sections).

## D96. M10 results

*M10 (spec §10): harness instrumentation: loop statistics, the edit journal, pack harness
metadata, `origin` provenance, the repetition report, owner findings, and machine-readable
check output.*

- **1. `loop_stats()` reports the rolling median and worst edit-to-see per file and per
  layer, and counts a failed reload: met** (D92). Edit-to-see runs from the saved edit to
  the frame that shows it (`mark_visible`), or to the reload's end until then; a parse
  failure counts against its file, its layer, and the total; polling does not push edits
  out. The harness now measures it end to end: its Config's clock is the file system's, so
  a file's write time is the origin, and it calls `mark_visible()` after each swap.
- **2. `unsaved()` lists a tweaked-but-not-written-back value: met** (M3b, and D93's
  test). The edit journal around it records every live edit, its old and new values, the
  old value's source, and whether it was written, reverted, or superseded.
- **3. `source()` names a value's `origin` tool: met** (D91), for a value from an
  included generated file, and in `layer_effect` and `diff` too.
- **4. A subtree repeated in three files appears in the repetition report with all three
  locations: met** (D94).
- **5. A one-owner pack is reported: met** (D94), once per report, as a finding.
- **6. Check JSON output is schema-versioned and stable across runs: met** (D95):
  `moira.check/1`, documented in `docs/check-json.md`, and compared byte for byte across
  two runs and against a checked-in file.
- **7. All instrumentation adds no allocations on the hot path and compiles out under
  `MOIRA_SHIPPING`: met.** Tweak reads and lookup are untouched (the codegen test still
  finds one load). `mark_visible()` is one branch and allocates nothing, and a fast-path
  `set` with the journal on allocates nothing, both tested; under `MOIRA_SHIPPING` the
  statistics and journal are empty and `mark_visible()` does nothing, tested in the
  shipping build. The tools (`check`, `find_repeats`, JSON, the reference) stay in every
  build (D90 Q5) and cost nothing unless called. One cost is recorded rather than hidden:
  the journal's time stamp makes a fast-path `set` 51–65 ns against 18–24 (D93).
- **8. The core stays within the 15,000-line budget: met.** 10,955 lines (10,116 before
  M10; the plan estimated about 11,000), so the repetition report did not need to move into
  a tool. `moira::shape` is 400 of 600, unchanged.
- **Deviations and settlements:** `origin` only as a file's leading declaration (D91);
  `check` takes a span of trees and `CheckOptions` (D94); `restore` reverts journal
  entries (D93); the repetition report in moira::data (D90 Q3).
- **The harness** (step 6): its Loop panel shows `loop_stats()` per layer and per file
  above the reload bars, and a Journal panel lists the session's edits with their fates;
  the headless test checks both.
- `docs/m10-plan.md` is deleted; its answers are D90. Every milestone of spec §10 is done.

## D97. A C++ object destroyed during a tick leaves its Context at once

*A use-after-free, found while planning gap 6 (D98).*

- **The bug.** D67 defers removals made during a Scheduler's tick to its sync point, so
  that tasks later in the same tick still reach an object a script `{delete}`s. But
  `ObjectBase`'s destructor called the same `Context::remove`, so a C++ object destroyed
  during a tick (by a C++ task, or a native function a script task called) left its slot
  pointing at freed memory, and the sync point then wrote into it (`ctx_ = nullptr`).
  ASan reports it as a heap-use-after-free in `Context::Impl::remove`.
- **The fix.** The destructor removes the object at once, even during a tick: a destroyed
  object cannot stay reachable, which is all the deferral was for. A removal deferred
  earlier in the same tick finds the slot gone (its salt changed) and skips it, as before.
  `Context::remove` of a living object still waits for the sync point.
- **Test** (`test_script_tasks.cpp`): an object with a loose parameter destroyed by a C++
  task mid-tick, and one removed then destroyed in the same tick. Without the fix the
  first fails under ASan as above.

## D98. Gap 6: loose parameters are the Context's

*Answered by the project owner 2026-10-13, as recommended: owned by the Context. Closes the
allocator half of acton-review gap 6.*

- **The gap.** An object's loose parameters (`{obj set mood happy}`) came from the default
  allocator (D62), the one per-object allocation that did not follow D61's rule that an
  owner passes its allocator. D62 chose that because parameters outlived the object's stay
  in its Context, and an object may outlive its Context's allocator.
- **The change.** Parameters are only ever read or written through the Context an object
  is in, so they are now the Context's: made on the first `set` from its allocator (the
  vector inside too), and freed when the object leaves it, by `remove`, by the object's
  destruction (D97), or by the Context's. A removed and re-added object starts with none.
  Nothing outlives the allocator it came from, and `ObjectBase` keeps its 32 bytes.
- **Docs:** `language.md` says parameters belong to the object's stay in its Context.
- **Tests** (`moira_alloc_tests`): setting parameters allocates through the Context's
  counting allocator; removing the object returns every byte; a re-added object has none;
  an object that outlives its Context leaves nothing to free. Against the previous code
  the test fails twice: the bytes stay after removal, and the parameter survives.
- **Benchmarks** (`bench_call`, release, three alternating runs against `8ebd447`): a
  native method, 49.96–50.40 ns before and 40.10–50.01 after; one of 2,000 objects, 51.8–68.2
  and 52.2–55.1; `handle` in a Context, 8.25–8.76 and 6.58–8.67. No change beyond noise:
  dispatch is untouched.
- **Gap 6** stays open for its other half, the vtable `type()` goes through.
- **Size:** the core is 10,963 lines (10,955 before).

## D99. A float result of zero skips the text search; cold rows for many types

*Found while measuring acton-review gaps 2m and 6, which D100 records.*

- **The bug.** A native function returning a float gives its shortest decimal
  (`float_to_decimal_double`, D51). The arithmetic fast path covered
  magnitudes from 1e-13 to 1e13, so zero, the most common float a game returns, fell to
  the `snprintf` search: about 1,300 instructions, two thirds of a cold method call. Zero
  (and -0, whose sign is kept) now returns at once. Subnormals and values outside the
  range still take the search.
- **Test** (`test_write`): 0 and -0 convert with their bit patterns.
- **New `bench_call` rows** (cold): 2,048 objects of 64 types, each with 32 methods
  (`{wN mK 0}`, a random method) or one (`{nN m0 0}`), called by script in a shuffled
  order, and `handle(HitMsg)` sent from C++ to the same objects. Each type has its own
  method table, thunks and vtable, so these rows find what 2,000 objects of one type
  (`{pN grow 0}`) keep warm. They return 0, which is how they found the bug.
- **Benchmarks** (release, three alternating runs against `3068879`, the same benchmark
  file): one method, 163–213 ns before, 104–114 after; 32 methods, 226–269 and 154–176;
  `{bob grow 0}` (returns 6.0, the fast path all along) and the other rows unchanged within
  noise. Per call in a cachegrind driver (200,000 cold calls): 1,917 instructions before,
  594 after, D1 misses unchanged (10.7).
- **Size:** the core is 10,963 lines.

## D100. Gaps 2m, 6 and 10 measured and closed

*Answered by the project owner 2026-10-13, all as recommended after measurement: accept
2m and 6, and accept 10 with the link flags documented. The acton-review table has no
open gap left.*

- **How.** `bench_call`'s cold rows for 64 types (D99), a cachegrind driver making 200,000
  cold calls on the same 2,048 objects, and a prototype of each alternative in a scratch
  worktree on top of D99. Both prototypes passed the test suite. Per call, instructions
  and D1 misses:

  | | script, 1 method | script, 32 methods | C++ `handle` |
  |---|---:|---:|---:|
  | now (D99) | 594 · 10.7 | 635 · 15.6 | 178 · 2.02 |
  | 2m: method names in their own array | 594 · 10.6 | 625 · 14.0 | 178 · 2.04 |
  | 6: a type pointer for `type()` | 553 · 9.8 | 594 · 14.9 | 152 · 1.70 |

  Timed, five alternating runs: 32 methods, 128–141 ns now and 123–180 with the names
  split; cold `handle`, 30–38 ns now and 25–27 with the type pointer.
- **2m, accepted.** A type with 32 methods costs a cold call about 5 more misses than a
  type with one; the 120-byte stride of the binary search is 1.6 of them, and the rest is
  the method's own entry and overload. The split saves those 1.6 and 9 instructions, which
  the timings cannot see. Reopen if a game's profile names `TypeRecord::method`.
- **6, accepted.** The vtable costs a cold message about 26 instructions and 5 ns (an
  indirect call, then `record()`'s guard). The prototype kept the virtual destructor (40
  bytes an object); doing it for real removes the vtable, so destruction goes through the
  `TypeRecord` and every derived type's constructors must set the pointer, a change to how
  every game object is declared, for 5 ns. Moira binds the game's objects, which often
  have vtables of their own.
- **10, accepted as a standing rule.** A minimal game (load, read, a tweak, a function, an
  object, `eval`; GCC 13, `-O2`, static libraries) links:

  | build | Moira code | tooling in it |
  |---|---:|---:|
  | release | 626.7 KB | 57.7 KB |
  | shipping | 580.2 KB | 41.5 KB |
  | release, function sections and `--gc-sections` | 440.7 KB | 4.8 KB |
  | shipping, the same | 407.1 KB | 4.8 KB |

  `check`, JSON, the reference and `find_repeats` link 0 bytes in every build: they are in
  `reference.o` (90.6 KB) and `repeats.o` (52.1 KB), which a game never references. What
  ships is in `config.o`: write-back and `promote` (22.9 KB in shipping), `diff` and
  `layer_effect` (12.9 KB), the journal (0.8 KB), all dropped by `--gc-sections`, and the
  source maps loading reaches (4.8 KB) stay. MSVC's release defaults drop unused
  functions already (`/O2` implies `/Gy`; `/OPT:REF` without `/DEBUG`). Splitting
  `config.cpp` (B) or a `moira::tools` library (C) would save a default-flags build about
  36 KB, and C would move 1,117 lines out of the core at the price of five `Config`
  members becoming free functions; neither changes a build with sections.
- **Docs:** the cookbook's "Linking a shipped game" gives the flags, checked with a
  CMake project that adds Moira as a subdirectory (409 KB of Moira code, shipping).
- **Gaps table:** 2m, 6 and 10 move to the closed list; gap 10's rule (a tool adds no
  field to a hot structure, and costs nothing unless called) moves to "Already aligned".

## D101. The real-corpus gate, run

*Spec §8 and M1, left open by D3 and D17. Answered by the project owner 2026-10-13: a lone
`#` is a symbol; Karaoke Revolution's older include syntax stays a failure; converted sets
are reported, outside the gate.*

- **The corpus.** `mariteaux/milo-script-library` at `85a4e5d7`, cloned read-only outside
  the repository (D3: nothing from it is committed): 32,009 `.dta` files from 36 games.
  Its README says most of them are not Harmonix's text. They are the shipped binary DTB
  turned back into text by community tools. Only a few sets are raw DTA with Harmonix's
  comments: Rock Band PS2 (the set spec §8 names), Guitar Hero II PS2 Deluxe and 360 DLC,
  and Karaoke Revolution Xbox.
- **`moira_parse_corpus --milo-paths`** reads the dump as its archives were laid out. The
  dump stores a `..` that climbs above an archive's root as a directory named `(..)`, so
  `rb1/ps2/config` including `../../../system/run/config/macros.dta` finds it under
  `rb1/ps2/(..)/(..)/`. Files the dump lacks (title updates hold only the files they
  changed) are read as empty and counted: 777 files include one of 2,263 such files. The
  run then measures the parser, not the dump. It takes 41 s.
- **One grammar change:** a lone `#` is the symbol `#` (`(# {fake_win 3})` binds a key in
  raw GH2 and in 339 converted files across 20 games). No directive has an empty name, so
  nothing is ambiguous; the writer still quotes it, `'#'`. Any other unknown `#name` stays
  an error.
- **The gate: raw DTA, 464 of 464** files parse and round-trip: RB1 PS2 304, GH2 PS2
  Deluxe 120, GH2 360 DLC 40.
- **Karaoke Revolution** (2004, three builds, 566 files): 387 parse. 172 use an older
  syntax, `#macros.dta` for include and `##kara.dta` for merge, which Moira does not take,
  and 7 have a string Harmonix never closed (`french_screens.dta:596`, `"...de la
  profession.)`).
- **Converted sets: 30,684 of 30,979** (99.05%). Every remaining failure comes from the
  conversion, and the parser stays strict rather than guess:
  - 99 RB4 song files close one array too many;
  - 78 locale files hold a symbol written bare that reads as a 20-digit integer;
  - 59 hold a symbol written bare that starts with `#` (`#$%&'...`, `#1`, `#00aa00`);
  - 42 Amplitude 2003 files hold a top-level `[1 0]`, which DTA text reads as a macro;
  - 17 RB4 and RB VR files hold strings with `"` inside, unescaped (`"...the "sexy cop dad
    from the '70s" in all of us."`), so the quotes no longer pair.
- **Tried and reverted:** letting a quote inside an atom belong to it, and closing a quoted
  symbol at the end of its line. Together they fixed 9 converted files, and they would
  hide a real stray quote in a studio's own data.
- **Whole corpus:** 31,535 of 32,009 (98.52%), no round-trip failures.
- **Parse speed** (`bench_parse`, three alternating runs against the previous commit):
  146.7, 155.6, 195.0 MB/s before; 148.0, 150.2, 196.3 after, 43 allocations each. The
  machine's speed drifted between runs, and each pair agrees within it.

## D102. `check` at load: validators by id, and `check_validator`

*Spec §5's "opt-in via a `Config` flag", left open since D42. Answered by the project owner
2026-10-13, as recommended.*

- **Several validators.** `Config::set_validator`, one slot that shapes took (D84), is
  replaced by `add_validator(v)`, which returns an id, and `remove_validator(id)`, as
  `on_reload` works. They run after every merge in the order they were added. Every
  caller changed.
- **`moira::check_validator(ctx, options)`** (moira::script) runs `check` over the merged
  root and sends its errors and one-way warnings to the Config's warning handler. The
  demand report, repeats, and one-owner findings are for tools, so it reports none of
  them and skips the repetition pass. The Config cannot call `check` itself, since
  moira::data does not depend on moira::script; the validator is how a flag reaches it,
  as `shape::validator()` already did for shapes. `ctx` and `options.packs` must outlive
  the Config's use of it.
- **Test** (`test_check`): a push of an overlay with a wrong call is reported at the push,
  located, next to another validator that runs first; a removed validator stops; a layer
  that fixes the call silences it, and toggling that layer off brings it back.
- **The harness** adds it beside the shape validator, so a wrong call typed into
  `default.dta` shows in the console at the next reload.
- **Spec §5's CI line**, "run in CI over the real-corpus test with Harmonix's built-ins
  stubbed by arity", runs over the original fixtures (D3): `test_check` stubs every head
  `tests/corpus/scripts.dta` calls and checks the report comes back clean. The Harmonix
  corpus stays outside the repository and CI (D3, D101).
- **Cost:** a merge walks a list of validators instead of testing one slot, and a Config
  with none does no more than before. The check itself costs a merge what `check` costs,
  in development builds that ask for it. **Size:** the core is 10,990 lines (10,963).

## D103. clang-tidy runs in CI

*Spec M0's "clang-format/tidy": `.clang-tidy` existed since M0, but nothing ran it.*

- **First run:** 901 findings over `src/`, 220 once the headers' repeats are counted once.
  `HeaderFilterRegex` also missed `src/`'s own headers; it now covers them.
- **Turned off, each for a pattern the code uses on purpose** (the reasons are in
  `.clang-tidy`): identifier naming, which fought the language's own names (`promise_type`,
  `is_expected`) and `kName` statics, so it goes rather than being tuned; C arrays; range
  loops (each flagged loop's container grows or shrinks while it runs, so tidy's own
  rewrite was wrong: `arrays_` in the cache's breadth-first layout, completion callbacks
  that unsubscribe); type erasure through `void*`; optional access after a type check;
  enum sizes that pin a layout; braced returns and `auto`.
- **Fixed:** a forwarding reference passed to `std::move` (`take` in script.hpp, now
  `std::forward`); `~Scheduler` without `override`; uninitialized members
  (`TaskPromiseBase::scheduler`, `CallStats`, `CacheFile`, the cache header read); implicit
  narrowing to `int64_t` in error messages; implicit widening of `int` products; an
  increment inside a condition; an assignment inside an `if`; a switch without a default;
  error helpers that took `Str` by value and moved it into a `string_view` parameter (now
  `string_view`); `==` and `!=` take `const Node&`; `[[nodiscard]]` and `const` where tidy
  asked.
- **Suppressed in place**, five lines with the reason beside each: copy-and-swap
  assignment, `ObjectBase`'s assignment that copies nothing, `handle_all`'s ordered branch
  chain, a `sizeof` of a stored pointer, and the cache writing a node's bytes into its file.
- **CI** runs `run-clang-tidy-18` over `src/` with every check an error; so does
  `tools/local_matrix.sh`, last, from the debug build's compile commands.
- **Benchmarks** (release, three alternating runs against `1fb9939`): `{add 1 2}`
  28.9–29.4 ns before, 28.5–29.3 after; parse 153.6–154.2 MB/s, 143.9–187.6; `{bob grow 0}`
  unchanged. `{add {add 1 2} 3}` read 59.5–60.7 ns before and 46.5–53.1 after, but
  cachegrind counts 708 instructions per evaluation in both: code layout, not the fixes.

## D104. No DTB compatibility module

*Spec §9's P3 stretch goal (`MOIRA_DTB_COMPAT`). Decided by the project owner 2026-10-13:
skipped.*

- **What research found.** The DTB layouts and both ciphers are well documented by
  community tools that agree with one another: dtab (BSD-3), Mackiloha and pikaxe (MIT),
  arson (LGPL-3.0). Old- and new-style name the encryption, not the layout; RB4-era files
  use a wider array header. No decompilation was consulted (spec §0).
- **Why not.** There are no clean real `.dtb` files to test against. The game files are
  Harmonix's, the corpus holds only their text conversions (D101), and the only public
  fixtures are three byte arrays in arson's LGPL tests. A reader checked only against an
  encoder written alongside it would not show that it reads what the games shipped, and
  testing the corpus from binary, the goal's stated purpose, cannot be done.
- Spec §0's line stands: Moira is not bit-compatible with Harmonix's `.dtb`.

## D105. What the DTB research says about `.datb`

*Results of D104's research, compared with Moira's own cache (spec §9, D70–D74, D79).*

- **Confirmed.** DTB has no version field, so tools guess its variants from bytes; its
  16-bit child counts forced a new layout for RB4; it writes every symbol inline at each
  use; its cipher doubles as the validity check (a first byte of `0x01` after decrypting);
  and it is a token stream decoded node by node. `.datb` has a magic, a version, and a
  hash; 32-bit counts and offsets; a symbol table interned once; a cipher kept apart from
  the hash; and loads as one read and a fixup pass.
- **Different on purpose.** DTB keeps `#ifdef`, `#include`, and `#define` as nodes and
  resolves them at load, so one file serves every define set; `.datb` stores the expanded
  tree and pays the preprocessor at bake time, one cache per define set.
- **Documented.** DTB is little-endian on every platform, so one file serves every
  console. `.datb` is a memory image for one byte order and pointer size, refused on any
  other. Every current target shares one layout, so no format change: the cookbook's
  binary-cache section now says to bake for the target's layout.

## D106. The game stacks' base

*`docs/background/game-stacks.md` §2 and §6, step 1. Outside Moira: `stacks/` is a
separate CMake project over this repository's Moira, and nothing in `src/` changed.*

- **What.** `stacks/base/`: `Live` (the Config, the layers `default`, `local`, `agent`,
  the shape and check validators, pack levels, and the file system's clock as the
  Config's), `Watcher` (`ReadDirectoryChangesW` on Windows, inotify on Linux), `FixedStep`,
  a bgfx renderer behind a small API, and `run`, the frame loop over SDL3 with Moira's
  ImGui panels and remote console, both from `examples/` as they are. `stacks/cube/` is
  the acceptance game: every value a tweak.
- **The watcher, not polling.** Moira's harness example polls, reading and hashing every
  file four times a second; that adds up to 250 ms to every edit and grows with the data.
  The base reloads only after the system reports a saved `.dta` file, and passes the
  file's write time as the reload's origin.
- **Integer time.** `FixedStep` first summed seconds as doubles; its test found 59 steps at
  144 Hz and 60 at 30 Hz for the same second. It counts nanoseconds now.
- **Deferred.** Jolt, Recast/Detour, miniaudio, and Tracy (§2.1) come with the first stack
  that needs them; Duel and Ledger need none.
- **Measured** (debug, the development container, not the minimum-spec PC): save to the
  next frame 0.2–0.3 ms headless; save to presented frame 16.6 ms with OpenGL and vsync
  under Xvfb, one refresh; the reload itself 1.4 ms. Windows: CI's MSVC job builds the
  stacks and passes every base test on a Windows runner. Under Wine, a MinGW cross-build
  fails one watcher case (a file saved in a subdirectory that existed before watching
  began), which real Windows passes: Wine's emulation of recursive watches.
- **Acceptance met** (§2.2, on the Windows test machine: Core i5-10400F, 8 GB, GTX 1650
  SUPER; the packaged Release build with D3D and vsync): ten saves in Notepad, edit to see 21–41 ms, median about 25 ms, worst
  40.8 ms, against a target of one second; the reload itself 12.6 ms median, 23.6 ms worst,
  mostly its scan of the layers' files.
- **MSVC and third-party headers.** `/WX` failed on C4324 in `bx/file.h`; the fetched
  dependencies are `SYSTEM` (FetchContent, CMake 3.25), so their headers are external.

## D107. The agent harness, and `Config::autorun`

*`docs/background/game-stacks.md` §3; `stacks/agent.md` is the design note. One addition
to Moira's API; the rest is in `stacks/`.*

- **`Config::autorun()`** (new). A Config reads each layer with a session of its own, so
  the commands a layer's `#autorun` collected stayed in that session, out of a host's reach,
  and `cfg.session().autorun()` was always empty: data layered through a Config could not
  define a script function. `autorun()` returns the enabled file layers' commands, bottom to
  top, as their last reads collected them; an overlay contributes none. It allocates its
  result and touches no hot path. The base runs them after the load and at the frame after
  any change to the stack, so `agent.dta` can redefine a function `default.dta` defines.
- **Commands, not a tool server.** Every stack's game is its own tool: `sim`, `check`,
  `reference`, `diff`, `promote`. An agent with a shell needs no server, CI calls the same
  commands, and the commands know the game's script functions because they are its binary.
  `tools/console.py` reaches a running game's console.
- **`sim`** steps a `Game` headless at the fixed rate, ticking the Context's scheduler (the
  play loop now ticks it too: script tasks never ran before), from a scenario
  (`sim/*.dta`: seconds, start and timed commands), and judges each run by objectives
  (`metrics/*.dta`: a measure command, then a pass command over `$value`). Runs are counted
  in steps, never timed, and read `default.dta` and `agent.dta` but not `local.dta`, so a
  scenario and a seed reproduce. Results are `.dta`. Exit 0, 1, or 2: passed, an objective
  failed, could not run. `Game` moved to `base/game.hpp`, without SDL, and gained
  `reset(seed)`, called before play and before each run.
- **`check` is stricter for agents** than `moira::check`: a call to a function nobody
  defined is demand to Moira, but in an agent's edit it is nearly always a typo, so it fails
  unless the function is a declared stub (`#autorun {func name () (status stub) ...}`).
- **`promote`** shows what it would write unless given `--write`, which the agent's
  instructions forbid: promotion is a person's decision (§9).
- **Measured** (Release, the development container): the cube's 20-second scenario, 1,000
  seeds (1.2 million steps; each run judged), in 13–17 ms for the whole process, load included.
  The cube's step is one addition, so this is the harness's own cost.

## D108. Keyed shapes

*Revisits D85. Found building Duel (`docs/background/game-stacks.md` §4.6, D109): its
shapes checked nothing.*

- **The gap.** A shape governs arrays tagged with its name (D85). Keyed data, as
  `(fighters (kestrel (moves (jab ...) (heavy ...))))`, tags each child by its own name, so
  no shape reached it: `(startup 900)` and an undeclared `(armor 1)` loaded silently. The
  game-stacks design's §4.6 shape, `(move (at fighters * moves) ...)`, assumed otherwise,
  and so did `docs/language.md`'s example, which said `(at bosses * phases)` governs
  `(phase1 ...)`. D85's workaround, `(move (name jab) ...)`, costs layering: repeated tags
  do not merge one by one, so an agent's layer could not retune one move.
- **`(keyed)`.** A shape declared `(keyed)` governs every child of the arrays at its
  `(at ...)` path, whatever the child's tag: `(move (at fighters * moves) (keyed) ...)`. A
  keyed shape without `(at ...)` is an error at declaration. Matching by name is unchanged;
  a keyed shape does not also match by its name. `Shape::keyed` is new in the public API.
- **Cost.** The checker keeps the keyed shapes in a list and tests each against the path of
  every tagged array; with none declared, nothing changes. `moira::shape` is 421 lines of
  its 600.
- **The stacks' `check` command** now runs the shapes over the merged data too: a shape
  error fails it (exit 1), and an undeclared field is printed as a warning, the demand for
  a word the game does not have yet.
- **Docs:** `docs/language.md`'s Shapes section corrected, with keyed children.

## D109. Duel and Ledger

*`docs/background/game-stacks.md` §6, step 2: the first two stacks, on the base (D106) and
the agent harness (D107). Each stack's README is its design note; nothing in `src/` beyond
D108.*

- **Duel** (`stacks/duel/`, §4.6). The fight is integers throughout, the owner's choice:
  frames, millimetres, per mille; the data's metres and fractions are rounded once at load.
  A test checks that the fight's frame advantage equals the frame table's formula. The CPU
  and the bots are one mechanism (pilots) with different numbers: delayed sight, guarding
  (crouching only for a low it has seen), punishing what it knows is unsafe, attacking and
  cancelling. The combo search runs the fight itself over every cancel and link, memoized.
- **Ledger** (`stacks/ledger/`, §4.2). A day is a tick; each company sells into a market of
  its own whose price falls with saturation and recovers daily; unlocks and milestones are
  commands in the data, evaluated about the company judged. Four strategies (greedy,
  hoarder, specialist, random) compete from one start on the same market days.
  `economy.xlsx` is exported by a standard-library script to `data/economy.dta` with an
  `origin` form and each line's sheet row; a test fails if the export is stale.
- **The base gained** `Game::load` (structured data after every change to the stack, once
  `#autorun` has run), `Game::ui` (windows every frame), watching `<data>/../packs`, the
  `eval` command, and shape checking in `check` (D108).
- **Deviations from §4.** Duel's CPU difficulties are keyed data, not layers: every layer
  above `default` is shared by all difficulties, so the agent's layer could not tune one;
  the Duel panel switches difficulty in one session instead. Both stacks keep their data
  keyed, with `(keyed)` shapes, not §4.2's `(good (name flour) ...)`, so a layer can retune
  one entry.
- **Bugs the work found.** A trade (both fighters hit on one frame) read the second
  attacker's move after the first hit had cleared it: an out-of-bounds read, found by ASan
  in a diagnostic driver, fixed by capturing both moves first. And the shapes gap of D108.
- **Tuning the starting data** took the pilots through four designs before the win-rate
  curve was reachable: reactive blocking alone cannot stop a 4-frame jab, so guarding is a
  posture, with reactions only for switching high and low. Ledger's bots needed saving for
  the best investment, no expansion while losing money, and selling buildings in debt;
  before that the hoarder went bankrupt holding wheat it would not sell.
- **Results.** Duel, seeds 1–30: 239 of 240 runs pass; the one failure is
  `easy_vs_combo` at 0.27 against a ceiling of 0.25, at a band's edge. The CPU's win rates,
  median: easy 0.40 against the masher and 0.17 against the combo player; normal 0.73,
  0.40, and 0.77 against the turtle; hard 0.94 and 0.53. No infinites; the best combo is
  jab > jab > jab > low_kick > palm, 255 damage; jab is +1 on block, the most of any move.
  Ledger, seeds 1–30: 60 of 60 pass. The greedy bot's first mill on day 45–51, bakery
  85–97, tailor 159–164, 100,000 coins 349–369; after ten hours it leads the specialist by
  1.85–1.93 times, and wins every seed: a designer's call whether that is dominance or the
  competent player the pace assumes (`stacks/ledger/README.md`).
- **Measured** (Release, the development container: a 4-core Xeon at 2.8 GHz; not the
  minimum-spec PC). Duel's 240 runs, 210 of them an hour of rounds (45 million frames, two
  pilots each), in 3.6–3.7 s for the process: about 79 ns a frame. The combo search, with
  the load, 7–15 ms. Ledger's 60 runs (30 first hours stepped at 60 Hz, 30 ten-hour soaks
  of four companies) in 0.51–0.53 s; a load alone 5.3–5.7 ms. Sanitizers: every stacks
  test passes under ASan and UBSan.
- **Open** (closed by D110): packs read through a Config never reach
  `cfg.session()`, since each layer reads with its own session copy, so `check` and
  `reference` see no packs (no single-owner finding, no one-way warnings). The session
  after the top layer holds them all; a Config accessor for it is the likely fix.
- **CI** packages Duel and Ledger for Windows beside the cube (`stacks-<game>-windows`).

## D110. `Config::read_session()`: the pack register through a Config

*Closes the gap D109 left open.*

- **The gap.** A Config reads each layer with a session of its own, a copy of the one
  below, so the packs and origins its files declared never reached `cfg.session()`, the
  session the docs told hosts to give `check`, `check_validator`, `to_json`, and `reference`.
  Through a Config the one-way rule for calls (D83), the single-owner finding (D94), and the
  harness register (D95) saw nothing; in the stacks, `check` listed no packs, and an agent's
  task pack with only `"agent"` as owner raised no alarm (game-stacks.md §3.4). D107 was
  the same shape of gap for `#autorun`.
- **`read_session()`** (new): a copy of the session after the last file layer was read,
  which holds every layer's packs, origins, macros, and defines, since each layer reads over
  a copy of the one below (a disabled layer's declarations included). The Config refreshes
  it at every merge, before the validators run, at an address stable for the Config's life,
  so `check_validator(ctx, {.packs = &cfg.read_session()})` stays valid across reloads.
  `session()` stays what each layer is read with (include dirs, defines, pack levels).
- **Callers.** The stacks' base (its validator, `check`, `reference`) and every doc that said
  `&cfg.session()` (`language.md`, `check-json.md`, `cookbook.md`, `game-stacks.md`).
- **Cost.** One session copy per merge (a push, reload, toggle, or restore), never on the
  scratch fast path. `bench_cache`, Release, three runs each, against `origin/main` in a
  worktree on the same machine: a 1.2 MB layer pushed without a cache 307–401 ms after and
  325–390 ms before; with a fresh cache 53–58 ms against 43–56 ms; an unchanged reload
  2,469–2,498 ns against 2,478–2,613 ns; a scratch set 64.2–64.3 ns against 62.2–64.3 ns.
  All within run-to-run noise.
- **Seen in the stacks.** `stacks_duel check` lists `duel_core`; `stacks_ledger reference`
  has the Packs and Origins sections (`economy.xlsx`, `sheets-export`); a task pack an agent
  writes with one owner is a finding in `check`.

## D111. Jolt in the base; Contraption and Stuntline

*`docs/background/game-stacks.md` §6, step 3, and §4.1, §4.5. Outside Moira: nothing in
`src/` changed. Each stack's README is its design note.*

- **Jolt Physics 5.5.0**, fetched at a pinned release, built with
  `JPH_CROSS_PLATFORM_DETERMINISTIC` (the owner's choice over a physics of our own).
  Jolt sets its floating-point flags only when it overrides the project's, which we do not
  let it do, so `stacks_deterministic` turns contraction off (`-ffp-contract=off`,
  `/fp:precise`) for Jolt and for each game's simulation code. Jolt also turns on AVX2,
  which the minimum-spec CPU has. `base::Physics` is one world: the static and moving layers,
  single-threaded stepping, and `hash()` over every body's state as bits.
- **Determinism, proven across compilers and platforms.** Tests pin three replay hashes
  computed with GCC on Linux: a toppling stack, Contraption's `drop` level, and a Stuntline
  race over a jump. GCC Debug, GCC Release, and Clang Release reproduce them locally, and CI's
  MSVC job reproduces them on Windows: the runner and the game agree bit for bit (§3.1, §7),
  the claim the design said had to be proven per stack.
- **Contraption** (`stacks/contraption/`): side-on puzzles (moving bodies locked to x-y,
  the owner's choice), pieces placed with the mouse and snapped, kinds and levels and goals
  as data. The analyses: the intended solution solves; it still solves in at least 80% of
  tries moved ±5 mm and turned ±2°; a random search over the tray finds no solution with
  fewer pieces. The three levels were laid out by the same searches, headless: `first_drop`
  has 4 solving plank placements in about 800; `high_shelf` needs two planks (3,000
  one-plank tries over seeds 1–30 found no cheese); `catapult` throws the ball with a seesaw.
  An attempt ends once everything is at rest, which took one from 65 ms to 3 ms.
- **Stuntline** (`stacks/stuntline/`): tracks laid end to end from a kit (straights, ramps,
  gaps, turns as chords), connectors checked, a racing line sampled every metre with each
  corner's speed; the car is Jolt's wheeled vehicle as its own sample sets it up; AI
  drivers at three skills; crashes respawn at the last flat piece; airtime, crash pieces, jump
  arrival speeds. Jumps are judged ballistically: cleared at the pro's speed, not at 70%.
- **What the objectives found, first run.** Stuntline: the canyon jump's 12 m gap left the
  rookie crashing at the landing forever (`skill_spread` 7.1), and the switchback's 12 m gap
  cleared even at 70% of the pro's speed (`jumps_need_commitment` 0). The gaps are 8 m and
  18 m now. Banked turns launched the car where the bank flipped between boxes; turns are
  flat until the road is a mesh. The car first spawned with its rear wheels off the road.
- **The base**: `Renderer` draws spheres and quaternion orientations; and its ImGui font
  texture was created with data, which bgfx makes immutable, so glyphs ImGui added later never
  appeared ("so ething heav" for "something heavy"): it is created empty, then filled.
- **Results** (Release, seeds 1–30). Contraption: 120 of 120 runs pass. Stuntline: 88 of
  90; the two left are rookie crashes at the canyon landing (`skill_spread`), at the band's
  edge.
- **What the MSVC job caught.** Stuntline's pinned race hash differed on Windows while the
  toppling stack's and Contraption's matched: Jolt was deterministic, our race code was not.
  It fed the C library's `sin`, `cos`, `atan2`, `tan`, and `hypot` into the simulation (the
  track's geometry, the steering, corner speeds), and glibc and the MSVC runtime do not
  return the same bits. Every function of a float that feeds a simulation now uses Jolt's
  (`JPH::Sin`, `Cos`, `Tan`, `ATan2`), and `sqrt`, which IEEE rounds the same everywhere;
  Contraption's goal check does too. A rule for every stack that replays.
- **Measured** (Release, the development container, a 4-core Xeon at 2.8 GHz). Contraption's
  120 runs (90 level judgements, each 30 robustness tries and up to 201 cheese tries, and 30
  played replays) in 11.0–11.3 s; an attempt about 3 ms. Stuntline's 90 runs (each track
  raced by three drivers, and 30 played races) in 2.1–2.3 s; a 10 s race about 8 ms.
- **CI** packages Contraption and Stuntline for Windows beside the others.

## D112. Recast/Detour in the base; Hearth and Wayfarer

*`docs/background/game-stacks.md` §6, step 4, and §4.3, §4.4. Outside Moira: nothing in
`src/` changed. Each stack's README is its design note. Every stack of §4 is now built.*

- **Recast/Detour 1.6.0** in the base (`base::NavMesh`), the owner's choice over a grid of
  our own: a grid of square tiles, each built from boxes and added or dropped alone; off-mesh
  links carry ability flags, and a path query names the abilities the walker has. Two bugs
  of our own, found by the stacks: a tile 64 m across at a 0.3 m voxel is 213.3 voxels, and
  a tile of 213 stops at 63.9 m, so neighbouring tiles never met (the voxel is now rounded to
  fit the tile); and Detour's nearest polygon within a box can have its nearest point outside
  the box (`nearest` now checks the reach).
- **Hearth** (`stacks/hearth/`): villagers and smart objects are Moira objects (`Villager`,
  `Thing`); an action is a `{class villager ...}` handler that starts a `thread_task` on
  `clock`, a Scheduler in game minutes ("walk to the fridge, open it, eat, close it" is
  data); a need below critical sends the typed message `NeedCritical`, script first, then
  the native default. Scoring is native and runs only when a villager decides.
- **Threaded soak, the owner's choice.** Households run on worker threads, each thread with
  its own Context (the types, a clock, and the data's `#autorun` commands evaluated over the
  one Config's tree, which atomic refcounts allow). A household's run depends only on its
  seed, so results are the same on any number of threads; a test checks it.
- **What the soak found, first run.** Over 10 seeds, 2 had a villager starving on day 1: a
  critical need interrupted what was under way, and the next decision chose something else
  (the bed over the fridge), so the need never recovered. The shape says a critical need
  "overrides the routine", and now it does: while one is critical, only things that help a
  critical need are options. Its tests build each failure on purpose: an action that never
  ends (stuck), a fridge walled in (unused, then starving), two needs that cut each other
  off (oscillating; a script `on_need_critical` that carries on ends it).
- **Wayfarer** (`stacks/wayfarer/`): a 3 by 3 world of 64 m box cells; a character on Jolt's
  `CharacterVirtual` that walks, climbs a climbable face it pushes into (up to
  `climb_max`), and glides; cells streamed as static bodies and a navigation tile within
  `stream_radius` of the player; six missions in three region files, with declarative
  objectives the director evaluates in script (`at_poi`, `flag`, `has_item`,
  `mission.done`, `time_of_day`).
- **The proof** searches every save point (a place, and what is held, set, and done), moving
  by paths whose links are the abilities held: 498 save points in the shipped world, none
  softlocked, every objective and point reachable. Its tests build a softlock (a one-way
  drop into a yard with a mission left outside), an unreachable objective, and a goal it
  cannot read. **Bots** of three styles measure pacing; their first run found a tower top
  no bot reached (its 1.5 m ledges were narrower than the walker after the mesh's erosion;
  they are 2 m now), and a curious bot that wandered among what it had found for 24 minutes
  once nothing new was in sight (it now follows the objectives, as a player would).
- **Runner and game agree.** Two played scenarios walk the proof's paths with the game's own
  character on Jolt: a climb to the lighthouse and a glide to the island. Both pass on every
  seed.
- **A gap in Moira, found.** A second `{class villager ...}` replaces the first (a
  reloaded script redefines its class), so an L3 task pack could not add one action to the
  villager: the agent's authoring loop of §4.3 needs a way to extend a class from another
  file. Fixed in D113 with `{extend}`, the owner's choice.
- **Results** (Release, seeds 1–30). Hearth: 60 of 60 runs pass (1,500 soaks of 50
  households for 30 game days, and 30 played days of the cottage). Wayfarer: 180 of 180
  (the proof, three bots, and the two played routes, per seed). Longest stretch without a
  discovery: direct 28 s, completionist 34 s, curious 77 s, against 90 s; the one glide
  clears with 21% to spare; following the objectives, every mission is done at 142 s.
- **Measured** (Release, the development container, a 4-core Xeon at 2.8 GHz). Hearth's
  30 seeds in 18.6–22.7 s; one soak (50 households, 118 villagers, 3,540 character-days) in
  1.96 s on one thread, 1.38 s on two, 0.79 s on four. One 60 Hz step of 150 villagers takes
  22–24 µs, of 1,500 villagers 430–500 µs: the §4.3 budget of 150 at full simulation is two
  orders of magnitude inside a frame here (not yet measured on the minimum-spec PC).
  Wayfarer's 30 seeds in 0.95–1.04 s; loading the world, building every tile, and the proof
  about 0.24 s; a streamed cell's navigation
  tile about 16 ms to build on the main thread, a hitch to move off it.
- **The base** also gained `Game::unbind` (a game drops what it keeps in a Context before
  the Context goes) and `Physics::temp()` (Jolt's scratch allocator, for characters).
- **CI** packages Hearth and Wayfarer for Windows beside the others.

## D113. `{extend}`: handlers added to a class from another file

*Spec §5, "Objects". The gap D112 found; the owner chose a new word over making `{class}`
merge.*

- **The gap.** `{class T ...}` again replaces every handler of `T`. That is right for a
  reloaded script (a handler deleted from the file must go), and wrong for a second file
  that means to add one: an L3 task pack writing one new villager action (game-stacks.md
  §4.3, "Authoring") wiped the others.
- **`{extend T (name ($params) body...) ...}`** adds each handler to `T`'s class, replacing
  only one of the same name. `T` is a class already defined or a bound C++ type (which gets
  a class then); anything else is an error that says to define the class first. `{class}`
  keeps its meaning, so a reload still replaces everything, extensions included; the
  layers' `#autorun` commands run bottom to top, so a task pack's extension runs again
  after the class it extends. Making `{class}` merge instead would have kept handlers a
  reloaded file had deleted.
- **`check`** reads `{extend}` as it reads `{class}`: the handlers an extension adds are
  not demand. The reference lists the word.
- **Cost.** Definition time only: dispatch is unchanged. The core is 11,068 lines of 15,000
  (`moira::script` 3,896). Hearth's test of a stuck action now adds that action with
  `{extend villager ...}`, as a task pack would.

