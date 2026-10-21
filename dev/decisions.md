# Decisions

Deviations from and clarifications of `dev/spec.md`, with the reasoning. Short entries,
newest last. Background documents live in `dev/background/`.

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
`baker::data`, `Type::Object` is an opaque 32-bit handle (16 index + 16 salt) that copies,
compares, prints, and round-trips but cannot be resolved; `baker::data` knows nothing
about `Context`. `baker::script` owns the per-`Context` object table and does strong
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
stored under the spec's name. All six companion documents are in `dev/background/`.

## D8. Known extension point: epoch-cached pure commands

*From `background/seexpr-vs-baker.md` §8.* A command whose inputs are all tweaks is
constant until reload and could be memoized on the reload epoch if its functions were
marked pure. Not built — it adds a purity annotation and an evaluation cache for a
workload Baker doesn't have. Recorded so it isn't reinvented by accident.

## D9. M0 build layout

- Three CMake targets: `baker_data` (`baker::data`), `baker_script` (`baker::script`,
  links `baker::data` PUBLIC), and the interface umbrella `baker` (`baker::baker`).
  `BAKER_BUILD_SCRIPT=OFF` builds the data layer alone; CI has a job that does exactly
  that, so a reverse dependency fails the build.
- Build switches: `BAKER_BUILD_SCRIPT`, `BAKER_BUILD_TESTS`, `BAKER_ATOMIC_REFCOUNT`,
  `BAKER_SHIPPING`, `BAKER_NO_EXCEPTIONS`, `BAKER_SANITIZE` (`address,undefined` etc.).
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
  `LoadSession::autorun` for the host or `baker::script` to run.
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
  `baker_parse_corpus --roundtrip <checkout>`; first run 2026-10-13, recorded in D101.
- **Fuzzing.** `baker_parse_fuzzer` (libFuzzer, ASan+UBSan, seeded from the corpus) checks
  that every input either fails with a located error or parses and round-trips. It found
  two bugs, both in BOM handling (`tests/fuzz_regressions/`), and then ran clean for 1.2M
  execs (the M1 gate). A 10M-exec run (spec §8) then finished clean: 10,000,000 execs in
  17 minutes, 940 edges covered, a 950-input corpus grown from the seeds, no failures.
  CI fuzzes for two minutes per push.
- **No-exceptions build** compiles with `-fno-exceptions` and runs the full suite; CI has
  a job for it.
- **Size.** `baker::data` is about 1,920 counted lines after M1, roughly half of the
  4,000 soft budget (D2). The parser and preprocessor are about 640 of those, `Node` about
  260. As D2 predicted, the full scope will pass 4,000; candidates for trimming later are
  the `expected` shim (about 100 lines, gone once C++23 is the floor) and the error-message
  strings in the parser.

## D18. Numbers are stored at 64 bits

*Deviates from spec §1 ("Int/Float are deliberately 32-bit ... do not widen the storage").
Decided 2026-10-11 after the core-types research (`background/core-types-references.md`).*

Int is stored as `int64_t` and Float as `double`. The spec's reason for 32 bits was node
size, but on the 64-bit targets Baker requires the payload is 8 bytes anyway because it
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
still fits the Node's 8-byte payload, so this costs nothing; baker::script (M5) is its
first user.

## D20. Refcounts are atomic by default

*Deviates from spec §2 (non-atomic by default, atomic as an option).*

`BAKER_ATOMIC_REFCOUNT` now defaults to ON. Copying a Node that holds a String or Array
writes its refcount, so the spec's "multi-thread read after freeze" model was only safe if
readers never copied a Node, an easy rule to break. Unreal made the same call: UE5's
`TSharedPtr` defaults to `ESPMode::ThreadSafe`. The evidence offered for non-atomic counts
in job-system engines turned out to be unverifiable (`background/core-types-references.md`).

Cost: no measurable difference on the parse benchmark (about 145–170 MB/s either way,
within run-to-run noise). M2's lookup benchmarks will measure copy-heavy paths. The
setting is always passed as a PUBLIC compile definition, because translation units that
disagree would violate the ODR; `-DBAKER_ATOMIC_REFCOUNT=OFF` remains for
single-threaded programs and has its own CI job.

## D21. The Expected<T> choice is made once, by the library's build

`Expected<T>` is `std::expected` in C++23 and Baker's shim in C++20. Compiled functions
like `try_read_file` return it, and the Itanium ABI does not mangle return types, so a
C++20 library linked into a C++23 program (or the reverse) would have linked silently
with two different layouts. CMake now checks once, at configure time, whether the
library's build has `std::expected`, and exports `BAKER_STD_EXPECTED=0/1` with the
target. A consumer that cannot match gets a compile-time `#error`; a C++23 consumer of a
C++20 build simply uses the shim too. Builds without CMake fall back to detecting it per
translation unit, and must then compile everything at one standard.

## D22. Portability fixes from the core-types review

- **No `std::filesystem` in the library.** Baker never opens files itself (the
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
spelling for display. Baker keeps DTA's case-sensitive identity instead: "first spelling
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
`baker::data` is now 2,063 counted lines; vendored fast_float is third-party and not
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
  the fallback forms are compiled out under `BAKER_NO_EXCEPTIONS`; the twin is
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
  M2 a Command or Var value is a clean error saying it needs `baker::script`, which is also
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
- **Fuzzing.** A second libFuzzer target, `baker_lookup_fuzzer`, takes a path and DTA
  text and checks that every typed lookup either succeeds or fails with a located error,
  that a path prints back as itself, and that sorted and linear `find` agree on every array
  in the tree. It ran 2.2M execs clean under ASan+UBSan locally; CI runs it for a minute
  per push, and a unit test runs the same properties over the corpus.
- **Builds.** Passing locally with warnings as errors: gcc 13 and clang 18 at C++20 and
  C++23, clang with libc++, data-only, ASan+UBSan, TSan, plain refcounts, shipping, and
  no-exceptions (gcc and clang). The parse fuzzer re-ran 1.36M execs clean after the
  empty-array change. MSVC is covered only by CI. Under TSan the allocation-count test
  reports itself skipped, because TSan's runtime owns `operator new`.
- **Size.** `baker::data` is 2,718 counted lines, up 387 from M1. Lookup, paths, sorted
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
with a policy flag; `baker::merge()` exposes it. Merging never mutates: changed arrays are
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

*Spec §6 (`BAKER_SHIPPING`: "reload and layer mutation no-ops after initial load").*

"After initial load" needs a moment to happen at, so `Config::freeze()` is it, in every
build: afterwards reload, layer changes, and live edits are refused. It is useful outside
shipping too, for anything that wants the config immutable. Under `BAKER_SHIPPING`,
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
- **Size: over budget.** `baker::data` is 4,002 counted lines, up 1,284 in M3, which crosses
  the spec's 4,000 (soft, D2). The largest parts are `config.cpp` 571 (write-back about
  200; layers, reload, provenance, live edits the rest), `tweak.hpp`/`tweak.cpp` 347,
  `config.hpp` 125, and `merge.cpp` 82. Flagged here and to the project owner, as §0 asks.
  Six milestones remain. Resolved by D40: the budget is raised to 8,000.

## D40. The line budget is 8,000

*Deviates from spec §0 (4,000) and M10 ("core stays within the 4,000-line budget").
Decided by the project owner, 2026-10-11, after M3 crossed 4,000 (D39).*

The goal has moved from the original's minimal core to a more capable library: layers,
provenance, write-back, and the harness instrumentation of M10 are all beyond what the
70 KB original did. The soft budget for the core (`baker::data` plus `baker::script`,
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

The request mentions "the two Baker-shaped rows of the environment slice". That term is
not defined in the spec or the background documents. M9's done-when therefore names what
the run shows instead: edit-to-see time with Baker's share measured, and a tuned value
written back as a one-line diff. If the slice is defined elsewhere, it should be added to
`dev/background/` and M9 should cite it.

## D42. M3b, as specified by the readiness brief (supersedes D41)

*Amends spec §1, §4, §10. Requested by the project owner, 2026-10-11, as a detailed brief
replacing the earlier request D41 recorded. The brief is kept as
`dev/background/m3b-environment-readiness.md`.*

A studio's readiness gate has four data-layer tests: edit-to-see time measured with
Baker's share visible; values tuned in play saved as minimal diffs; experiments that do not
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
- **Shipping and layer pushes.** "Layer mutation is inert under `BAKER_SHIPPING`" is read
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
- **Size.** `baker::data` is 4,747 counted lines (core 4,759), up 745 in M3b. That is over
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
says. `baker::data` stays independent of `baker::script` through one hook:
`detail::set_evaluator`, installed by the first Context constructed.

- **Only on the slow path.** A lookup converts the raw values first. Only if that fails,
  and some value is a Command or Var, does it evaluate them and convert again. Config
  lookups cost what they did. Only the values themselves are evaluated, not arrays nested
  inside them.
- **Raw reads stay raw.** `find`, `at`, `Convert<Node>` and `find<Node>` never evaluate.
- **Errors.** A value that evaluates to the wrong type is reported as what it evaluated
  to: `'name' is symbol, not int`. Without `baker::script`, the M2 message stands.
- **Which Context.** The calling thread's current Context:
  - the innermost `Context::Scope`;
  - else the Context running the current `try_eval`;
  - else the first Context created on the thread, which stays current until destroyed.

  With none, the lookup fails cleanly: "needs a current baker::Context on this thread".

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
  - Running the script tests with `BAKER_UPDATE_REFERENCE=1` rewrites it.

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
- **Size.** `baker::data` is 4,807 counted lines (+48, the hook and the symbol index).
  `baker::script` is 1,396. The core total is 6,203 of 8,000, up 1,444 in M4, under the
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
- **Size.** The core is 7,142 of 10,000 lines (D48), up 939 in M5: `baker::script`
  +917, `baker::data` +22. That is about 160 lines over the plan's estimate of 780. The
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
- **Exceptions.** The plan was to catch `baker::Exception` around each native thunk.
  Measured, a `try` there added about 10 ns to every native call, so it is not done.
  Instead, `baker::data` has a stack-recorder hook (as it has an evaluator hook, D45), and
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
  out under `BAKER_SHIPPING`.
- **Q3, tracing.** Compiled out under `BAKER_SHIPPING`. The API still compiles and does
  nothing.

M6's remaining steps (messages, statistics, tracing) wait for the hygiene work in D54–D56
(`dev/m5b-plan.md`), so they are built on the new API from the start.

## D54. No exceptions, and no `try_` prefix

*Deviates from spec §6 ("every throwing API has a non-throwing twin", `BAKER_NO_EXCEPTIONS`).
Decided by the project owner 2026-10-11.*

- **The throwing forms are removed.** Every fallible API returns `Expected<T>`. This
  removes:
  - `baker::Exception`;
  - `detail::raise`;
  - the `BAKER_EXCEPTIONS` and `BAKER_NO_EXCEPTIONS` switches and the CI job for them;
  - D52's stack-recorder hook, which existed only for exceptions.

  The library builds with exceptions disabled (`-fno-exceptions`, `/EHs-c-`) in every CI
  job. A game that uses exceptions elsewhere can still link it.
- **The `try_` prefix goes too.** With one form there is nothing to tell apart, and the
  one-line API reads better: `ctx.eval`, `ctx.func`, `ctx.add`, `arr.find<T>`, and so on.
  It is one mechanical rename across the API, tests, examples, and docs. `[[nodiscard]]`
  on every `Expected` keeps a failure from being dropped silently.
- **Native functions report failure by returning `Expected`,** as they already could. A
  C++ exception escaping a native function is now the host's bug; Baker does not catch
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
  chose Baker-owned vocabulary types, then decided the same day that the std ones are fine:
  they do not allocate, and reimplementing them buys nothing.)
- **`Error` becomes a handle.** It is an owning pointer to an error record (code, message,
  location, path, stack) allocated through the allocator (D56) on the error path only.
  `Expected<Node>` shrinks from about 120 bytes to 24, which also closes gap 9.
- **Callbacks** are a function pointer plus `void*`, as `NoticeSink` already is. No
  `std::function`.
- **Inside `src/`,** containers go through one internal alias layer (`src/common/containers.hpp`)
  that maps to the STL by default, or to EASTL with `BAKER_USE_EASTL=ON`, always with
  Baker's allocator (D56). This follows the owner's rule: where reimplementing an STL
  class would otherwise be needed, at least allow EASTL instead
  (<https://github.com/electronicarts/EASTL>). Naked `std::` containers in `src/` are
  not allowed.
- **Checked in CI:** a test compiles each public header alone and fails if it reaches any
  standard header outside the allowed list. It checks the preprocessor's dependency
  output, not by eye. An EASTL job builds with `BAKER_USE_EASTL=ON`.
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
  - `BAKER_EXCEPTIONS` and `BAKER_NO_EXCEPTIONS`, and both "no exceptions" CI jobs;
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
  the refusal under `BAKER_SHIPPING`.
- **Performance, on this container:**
  - `{add 1 2}` is 32–34 ns, unchanged (target 40, zero allocations).
  - **`-fno-exceptions` itself slows lookup.** HEAD's own benchmark, built with
    `BAKER_NO_EXCEPTIONS=ON`, reads `try_find_float` on a 20-child array at about 17 ns
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
    `baker_header_allowlist`) walks each header's include closure and fails on anything
    else.
  - A CMake object library compiles every public header on its own.
  - The count went 81 → 59 → 53 → 51 → 24 → 17 → 0 over sub-steps 2a–2f.
  - The allow-list also admits the trivial C headers `<cstdlib>`, `<cstring>`,
    `<cassert>`, and `<climits>`, and `<version>`.
- **The cost to a game file.** A translation unit that only reads a `Tweak<float>`
  preprocesses to 35,005 lines instead of 81,222 (the allowed headers alone are 31,434),
  and compiles at `-O2` in about 0.5 s instead of 1.35 s.
- **Opt-in interop headers.** Headers named `std_*.hpp` are exempt by name, and nothing
  else in Baker includes them:
  - `<baker/std_interop.hpp>` (data): `std::hash<Symbol>`, `Convert<>` for
    `std::string`/`vector`/`array`, `string_out()`, `to_string()` for nodes, arrays,
    paths, and stack entries, `to_dta()`, and `strings(List<Symbol>)`;
  - `<baker/std_script.hpp>` (script): `to_string(CheckReport)`, `reference(ctx)`, and
    `script_backtrace()` returning strings. It is separate so that `baker::data` never
    includes a script header.
- **New small types.** Each is a minimal piece of what the STL would provide, and each
  is tested:
  - `TextOut` (`text.hpp`): a function pointer and a context. All text Baker produces is
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
- **The interface** is D56's struct, in `<baker/allocator.hpp>`. An allocator must not
  return null. If one does, Baker aborts, as `operator new` failing did with exceptions
  off. `free` receives the size that was allocated.
- **The default allocator** is `operator new` unless the host calls
  `set_default_allocator()` before Baker first allocates. After that it returns false and
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
  are the STL's by default, or EASTL's with `BAKER_USE_EASTL=ON`. Each holds a pointer to
  a Baker `Allocator`, which is the default allocator unless the owner passes its own.
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
  - `<baker/allocator.hpp>` has `Allocator`, `default_allocator()`, and
    `set_default_allocator()`, which returns false once Baker has allocated.
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
- **EASTL.** `BAKER_USE_EASTL=ON` fetches EASTL 3.27.01 at a pinned commit with
  FetchContent. EASTL in turn fetches EABase. A host that already has an `EASTL` target
  uses its own. Neither is vendored; `THIRD_PARTY.md` lists them.
  - EASTL is private to the two libraries. Public headers are unchanged, so a host
    built either way sees the same API.
  - The STL and EASTL builds run the same assertions, with the same results (75,978 data,
    674 script, 11 allocator).
  - There is a CI job (`eastl`) and a local-matrix entry for it.
  - It is not built on MSVC in CI.
- **The test** is `baker_alloc_tests`. Its `main` installs a malloc default allocator
  before anything runs. In a separate file, because doctest's implementation brings in
  `<windows.h>`, whose `TextOut` macro renames `baker::TextOut`.
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
  `TextOutA`/`TextOutW`. A Windows host that includes it before a Baker header calls
  functions that do not exist in the library, and fails to link. Step 2 introduced the
  name. Renaming it is a public API change, so it waits for the owner.

## D63. No compatibility layers; `TextOut` becomes `TextSink`

*Decided by the project owner 2026-10-11.*

- **The rule.** Breaking changes are allowed and encouraged. Baker keeps no
  backward-compatibility layers, deprecated aliases, or unused legacy code paths. An API
  change updates every caller in the repository and deletes the old form. `CLAUDE.md`
  states the rule for every session.
  - Script-level `Status::Deprecated` stays: it lets script authors mark their own
    functions, and is not a compatibility layer in Baker's API.
  - A search of the code found no other compatibility layer to remove.
- **`TextOut` is renamed `TextSink`** with no alias, which settles D62's open question.
  `<windows.h>` defines `TextOut` as a macro, so a Windows host that included it before a
  Baker header failed to link. `TextSink` matches `NoticeSink`. Functions that return
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

*Recorded 2026-10-11. Implements D64. M5b is done, and `dev/m5b-plan.md` is deleted.*

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
  - `baker_alloc_tests` checks that 50 reloads of a changing file keep the Config's live
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
answers. The call stack was step 1 (D52). M6 is done, and `dev/m6-plan.md` is deleted.*

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
    `<baker/clock.hpp>`. Self time subtracts what callees took.
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

*M7 plan (`dev/m7-plan.md`) Q1–Q7, answered by the project owner 2026-10-12.*

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
done, and `dev/m7-plan.md` is deleted.*

- **The C++ side** is `<baker/task.hpp>`: `Task`, `Scheduler`, `TaskHandle`, and the
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
  - `baker_alloc_tests` runs C++ and script tasks on a Context with a counting allocator:
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

*M8 plan (`dev/m8-plan.md`) Q1–Q4, answered by the project owner 2026-10-12, each as
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
- **Q3, writers.** A `baker_bake` tool (in `tools/`, outside the core) for build steps, and
  `read_cached`, which rewrites a stale cache in development.
- **Q4, Config.** `ConfigOptions::cache_dir`: outside edit mode, file layers load through
  `read_cached`, and reload rewrites the caches of changed files. Edit mode reads text.

## D71. M8.1: the cache writer and the in-place load

*Results of M8.1 (`dev/m8-plan.md` §1–2).* `include/baker/cache.hpp`: `write_cache` (to a
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

## D73. M8.2: staleness, replay, `read_cached`, the `Cipher`, `baker_bake`

*Results of M8.2 (`dev/m8-plan.md` §3–4, D70, D72).*

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
  error. Under `BAKER_SHIPPING`: the cache if it loads, else the text, and nothing is
  written. Reads go through the session's reader, but caches are written with `fopen`:
  a host with its own file system writes through `write_cache(root, TextSink, ...)`.
- **`Cipher`** (spec §9): two functions over the bytes after the header, in place, and a
  user pointer, set as `LoadSession::cipher`. The hash covers the enciphered bytes, so
  damage is found before deciphering; an enciphered cache read without a cipher is an
  error. No implementation ships; the header keeps the anecdote.
- **`baker_bake [-D NAME]... [-I DIR]... IN.dta OUT.datb`** (`tools/`, outside the core)
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

*Results of M8.3 (`dev/m8-plan.md` §5, D70 Q4).*

- **`ConfigOptions::cache_dir`** (copied by the Config): outside edit mode, each file
  layer loads through `<cache_dir>/<hash of its path, hex>.datb`. A fresh cache gives the
  tree and replays the layer's defines on the session the layers above see; otherwise the
  text is parsed, recorded, and a new cache written through the Config's writer
  (`set_writer`), so a host's own file system gets it too. A cache that cannot be written
  is not an error. Under `BAKER_SHIPPING` a layer trusts its cache, and writes none.
- **Reload needed no new code.** The layer's reader still records every file read, now
  except the cache itself: on a fresh cache, the freshness check's re-reads are the
  layer's dependencies; on a miss, the parse's. A changed file makes `reload()` re-read
  the layer through `read_cached`'s path, which finds the cache stale and rewrites it.
  The record lists the main file first (it was last), so a layer's `file` names it either
  way.
- **The directory must exist;** the Config does not create it.
- **Shipping gets its caches from a development build's Config:** a build step pushes the
  same layers, with the same paths and `cache_dir`, and the directory ships. `baker_bake`
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
  `baker_bake` is in `tools/`, outside the core.
- `dev/m8-plan.md` is deleted; its answers are D70 and D72.
- **Naming.** Commits called M8's steps M8a–M8d; this file calls them M8.1–M8.4, because
  the spec's milestone M8b is `baker::shape`, which is next.

## D80. M8b: shapes and packs, answers

*`dev/m8b-plan.md` Q1–Q4, answered by the project owner 2026-10-12, each as recommended.*

- **Q1, what a shape governs:** every array tagged with the shape's name, anywhere in the
  tree; `(at path)` in the declaration narrows it to arrays under a path.
- **Q2, "reported at load":** Config runs a validator after each merge (a callback
  baker::data defines and baker::shape provides), so pushes, reloads, and restores report
  violations, located, through the Config's warning sink. baker::data knows nothing of
  shapes.
- **Q3, the name:** what spec §1 calls a pack's *layer* is its **level**:
  `(pack name (level game))`, `session.set_pack_levels({"engine", "game", "task"})`. A
  Config's layers are its stack of files, and `Tier` already names a function's
  implementation. This deviates from the spec's wording.
- **Q4, the one-way rule's warnings:** macro uses, found while parsing, are warnings on
  the `LoadSession` (`session.warnings()`), which Config forwards to its warning sink;
  calls to a softer pack's function are a third list in `CheckReport`.

## D81. M8b, step 1: `#require`

*Results of `dev/m8b-plan.md` §1.*

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

*Results of `dev/m8b-plan.md` §2 (D80).*

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

*Results of `dev/m8b-plan.md` §2, its last bullet (D80, Q4).*

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
`baker::shape` (`dev/m8b-plan.md` §3, D80).*

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
- **The library.** `baker_shape` (`include/baker/shape.hpp`, `src/shape/`), namespace
  `baker::shape`, depends only on baker::data, built unless `BAKER_BUILD_SHAPE=OFF`, and
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
  with the merged root and the warning handler; `baker::shape::validator()` returns one
  that reads the root's `(shapes ...)` and checks the root against them.

## D85. M8b, step 4: `baker::shape`

*Results of `dev/m8b-plan.md` §3 (D80, D84).*

- **The library** is `baker_shape` (`include/baker/shape.hpp`, `src/shape/shape.cpp`):
  400 lines of spec §6's 600, outside the core, which `tools/count_lines.py` now reports
  separately. It links baker::data only; the data-only build builds it too.
- **What D84 described, built**, with two settlements:
  - A field declared again (by `Set::add` of another tree) keeps the metadata the new
    declaration does not restate, as Config's merge of two layers would.
  - `bool` is what `Convert<bool>` reads: numbers, not the symbol `TRUE` (DTA defines
    `TRUE` as a macro for 1).
- **Matching by tag (D80 Q1) does not reach keyed children.** In `(bosses (dragon ...)
  (golem ...))` the children are tagged by their names, so no shape governs them; the
  data must write `(boss (name dragon) ...)`. Recorded as a limitation, for the owner to
  revisit if real data asks.
- **Config.** `cfg.set_validator(baker::shape::validator())` checks every merge: each
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

*M8b (spec §10): `baker::shape` (shape declarations, shape-aware `check`, the
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
  `baker::shape` 400 (of 600, outside the core).
- `dev/m8b-plan.md` is deleted; its answers are D80 and D84.

## D87. M9: the docs and examples, answers

*`dev/m9-plan.md` Q1–Q4, answered by the project owner 2026-10-12, each as recommended.*

- **Q1, `harness-imgui`:** Dear ImGui fetched at a pinned release (as EASTL is), GLFW and
  OpenGL 3 for the window, behind `BAKER_BUILD_HARNESS` (off by default). The panels are
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
    commits, behind `BAKER_BUILD_HARNESS`; Tweaks, Shapes, Console, Loop, Layers, and
    Snapshots panels, provenance on hover, write-back. CI builds it on Linux and MSVC and
    runs its panels headless. The local matrix gains a harness configuration.
- **A gap the docs exposed, fixed (D88):** a tweak of several numbers could not carry
  metadata in the data. The owner chose to fix it in M9; it is the milestone's only core
  change.
- **Not changed:** no benchmark row; the core changed only in `TweakBase::bind`, which
  runs per tweak on each merge. The core is 10,116 lines of 15,000 (10,103 before; D88),
  and `baker::shape` 400 of 600.
- `dev/m9-plan.md` is deleted; its answers are D87.

## D90. M10: harness instrumentation, answers

*`dev/m10-plan.md` Q1–Q5, answered by the project owner 2026-10-12.*

- **Q1, loop statistics' samples (as recommended):** a fixed table allocated once, up to
  32 files and 16 layers, each with its last 32 edit-to-see and reload times (about 10 KB
  per Config); the least recently changed file gives up its row. Only reloads that changed
  something feed it, because a host polling four times a second fills M3b's 256-record
  history with `Unchanged` records in about a minute (found in M9's harness).
- **Q2, journal values (as recommended):** inline, up to four nodes each for the old and
  new values, with the true count; about 160 bytes an entry in a ring allocated once, so
  `set`'s fast path stays allocation-free.
- **Q3, the repetition report: in baker::data,** not in `check` as the spec puts it and
  the plan recommended. `baker::find_repeats(trees, options, out)` needs no Context, so
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

*Results of `dev/m10-plan.md` §3 and §4 (D90).*

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

*Results of `dev/m10-plan.md` §1 (D90 Q1).*

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

*Results of `dev/m10-plan.md` §2 (D90 Q2).*

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

*Results of `dev/m10-plan.md` §5 and §6 (D90 Q3, Q4).*

- **`baker::find_repeats(trees, options)`** (`<baker/repeats.hpp>`, baker::data, as Q3
  chose) returns a `List<Repeat>`: one occurrence as a node (`form`), its size in nodes
  counted recursively, and every site (file, line). The plan wrote it with an out
  parameter; a returned `List` is how Baker hands back results elsewhere. Options:
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

*Results of `dev/m10-plan.md` §7, and the reference's part of §3 and §4 (D90).*

- **`write_json(report, sink, &session)`**, and `to_json(report, &session)` in
  `<baker/std_script.hpp>`: one object, `"schema": "baker.check/1"`, then `errors`,
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
  (regenerated with `BAKER_UPDATE_REFERENCE=1`, as the reference is); an empty report; the
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
  `baker.check/1`, documented in `docs/check-json.md`, and compared byte for byte across
  two runs and against a checked-in file.
- **7. All instrumentation adds no allocations on the hot path and compiles out under
  `BAKER_SHIPPING`: met.** Tweak reads and lookup are untouched (the codegen test still
  finds one load). `mark_visible()` is one branch and allocates nothing, and a fast-path
  `set` with the journal on allocates nothing, both tested; under `BAKER_SHIPPING` the
  statistics and journal are empty and `mark_visible()` does nothing, tested in the
  shipping build. The tools (`check`, `find_repeats`, JSON, the reference) stay in every
  build (D90 Q5) and cost nothing unless called. One cost is recorded rather than hidden:
  the journal's time stamp makes a fast-path `set` 51–65 ns against 18–24 (D93).
- **8. The core stays within the 15,000-line budget: met.** 10,955 lines (10,116 before
  M10; the plan estimated about 11,000), so the repetition report did not need to move into
  a tool. `baker::shape` is 400 of 600, unchanged.
- **Deviations and settlements:** `origin` only as a file's leading declaration (D91);
  `check` takes a span of trees and `CheckOptions` (D94); `restore` reverts journal
  entries (D93); the repetition report in baker::data (D90 Q3).
- **The harness** (step 6): its Loop panel shows `loop_stats()` per layer and per file
  above the reload bars, and a Journal panel lists the session's edits with their fates;
  the headless test checks both.
- `dev/m10-plan.md` is deleted; its answers are D90. Every milestone of spec §10 is done.

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
- **Tests** (`baker_alloc_tests`): setting parameters allocates through the Context's
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
  every game object is declared, for 5 ns. Baker binds the game's objects, which often
  have vtables of their own.
- **10, accepted as a standing rule.** A minimal game (load, read, a tweak, a function, an
  object, `eval`; GCC 13, `-O2`, static libraries) links:

  | build | Baker code | tooling in it |
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
  `config.cpp` (B) or a `baker::tools` library (C) would save a default-flags build about
  36 KB, and C would move 1,117 lines out of the core at the price of five `Config`
  members becoming free functions; neither changes a build with sections.
- **Docs:** the cookbook's "Linking a shipped game" gives the flags, checked with a
  CMake project that adds Baker as a subdirectory (409 KB of Baker code, shipping).
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
- **`baker_parse_corpus --milo-paths`** reads the dump as its archives were laid out. The
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
  syntax, `#macros.dta` for include and `##kara.dta` for merge, which Baker does not take,
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
- **`baker::check_validator(ctx, options)`** (baker::script) runs `check` over the merged
  root and sends its errors and one-way warnings to the Config's warning handler. The
  demand report, repeats, and one-owner findings are for tools, so it reports none of
  them and skips the repetition pass. The Config cannot call `check` itself, since
  baker::data does not depend on baker::script; the validator is how a flag reaches it,
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

*Spec §9's P3 stretch goal (`BAKER_DTB_COMPAT`). Decided by the project owner 2026-10-13:
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
- Spec §0's line stands: Baker is not bit-compatible with Harmonix's `.dtb`.

## D105. What the DTB research says about `.datb`

*Results of D104's research, compared with Baker's own cache (spec §9, D70–D74, D79).*

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

*`dev/background/game-stacks.md` §2 and §6, step 1. Outside Baker: `stacks/` is a
separate CMake project over this repository's Baker, and nothing in `src/` changed.*

- **What.** `stacks/base/`: `Live` (the Config, the layers `default`, `local`, `agent`,
  the shape and check validators, pack levels, and the file system's clock as the
  Config's), `Watcher` (`ReadDirectoryChangesW` on Windows, inotify on Linux), `FixedStep`,
  a bgfx renderer behind a small API, and `run`, the frame loop over SDL3 with Baker's
  ImGui panels and remote console, both from `examples/` as they are. `stacks/cube/` is
  the acceptance game: every value a tweak.
- **The watcher, not polling.** Baker's harness example polls, reading and hashing every
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

*`dev/background/game-stacks.md` §3; `stacks/agent.md` is the design note. One addition
to Baker's API; the rest is in `stacks/`.*

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
- **`check` is stricter for agents** than `baker::check`: a call to a function nobody
  defined is demand to Baker, but in an agent's edit it is nearly always a typo, so it fails
  unless the function is a declared stub (`#autorun {func name () (status stub) ...}`).
- **`promote`** shows what it would write unless given `--write`, which the agent's
  instructions forbid: promotion is a person's decision (§9).
- **Measured** (Release, the development container): the cube's 20-second scenario, 1,000
  seeds (1.2 million steps; each run judged), in 13–17 ms for the whole process, load included.
  The cube's step is one addition, so this is the harness's own cost.

## D108. Keyed shapes

*Revisits D85. Found building Duel (`dev/background/game-stacks.md` §4.6, D109): its
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
  every tagged array; with none declared, nothing changes. `baker::shape` is 421 lines of
  its 600.
- **The stacks' `check` command** now runs the shapes over the merged data too: a shape
  error fails it (exit 1), and an undeclared field is printed as a warning, the demand for
  a word the game does not have yet.
- **Docs:** `docs/language.md`'s Shapes section corrected, with keyed children.

## D109. Duel and Ledger

*`dev/background/game-stacks.md` §6, step 2: the first two stacks, on the base (D106) and
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

*`dev/background/game-stacks.md` §6, step 3, and §4.1, §4.5. Outside Baker: nothing in
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

*`dev/background/game-stacks.md` §6, step 4, and §4.3, §4.4. Outside Baker: nothing in
`src/` changed. Each stack's README is its design note. Every stack of §4 is now built.*

- **Recast/Detour 1.6.0** in the base (`base::NavMesh`), the owner's choice over a grid of
  our own: a grid of square tiles, each built from boxes and added or dropped alone; off-mesh
  links carry ability flags, and a path query names the abilities the walker has. Two bugs
  of our own, found by the stacks: a tile 64 m across at a 0.3 m voxel is 213.3 voxels, and
  a tile of 213 stops at 63.9 m, so neighbouring tiles never met (the voxel is now rounded to
  fit the tile); and Detour's nearest polygon within a box can have its nearest point outside
  the box (`nearest` now checks the reach).
- **Hearth** (`stacks/hearth/`): villagers and smart objects are Baker objects (`Villager`,
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
- **A gap in Baker, found.** A second `{class villager ...}` replaces the first (a
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
  (`baker::script` 3,896). Hearth's test of a stuck action now adds that action with
  `{extend villager ...}`, as a task pack would.

## D114. The harness audit of the stacks

*`dev/background/reading-a-studio.md` §7, run on all seven games after step 4 of
game-stacks.md §6, as §6 asks. The full audit is `stacks/audit.md`. Outside Baker: nothing
in `src/` changed.*

- **What it could not do.** The method watches people and interviews them; the stacks have
  one person and an agent. Loop times are measured by a script, the dimensions profile is
  the builder's reading, and voluntary use is unknown. Re-run it when anyone else plays.
- **An instrument for step 2.** `{live.loop}`, bound by the base's `Live`, returns
  `Config::loop_stats()` as text, so a script reads a running game's loop through the
  console. Ten saves of a tweak in `local.dta` per game (Release, OpenGL under Xvfb, the
  development container): edit-to-see median 5.4 ms (cube), 6.3 (Duel), 12.3 (Ledger),
  10.0 (Contraption), 12.4 (Stuntline), 17.0 (Hearth), worst 24.4; Baker's reload 0.12–0.23
  ms median in every game. **Wayfarer: 174 ms median, 215 ms worst**, all the game's own:
  its gating view rebuilds the analyses' nine navigation tiles (about 16 ms each) after every
  reload, even for a run speed. Inside the one-second target, and ten times the others.
- **The other loops.** A C++ change: a 2–6 s rebuild and a restart. The agent: `check`
  0.01–0.09 s, one seed of `sim` 0.01–1.05 s.
- **Demand.** `check` over every stack's data, scenarios, and objectives: no errors,
  warnings, unresolved words, stubs, repetitions, or findings. The demand is in the C++,
  which `check` does not read: `tag_of` (an array's first symbol) written in 6 games, a
  seeded RNG in 6, `not_found` and a `box` helper in 5 each, float and positional readers in
  4 and 2, command truth in 3: 31 copies of words Baker and the base lack.
- **The register's finding**: a bus factor of 1 at every layer. Every pack names two owner
  roles, as `check` asks, and one person fills them all; only the register shows it.
- **Flight**: one external harness, Ledger's spreadsheet, adopted by design and visible
  through `origin`. **Evolution**: nothing commodity built in-house (SDL3, bgfx, ImGui, Jolt,
  Recast/Detour adopted); the analyses (cheese search, proof, bots, soak) are genesis-stage
  ideas fixed in C++, one each.
- **Interventions proposed**: fix Wayfarer's loop (keep the analyses' mesh until its
  geometry changes); push the 31 copies up to Baker (`Array::tag()`, a positional reader) and
  the base (RNG, `Box` constructors, `not_found`, command truth); a second gardener when
  there is a second person.

## D115. The audit's interventions: Wayfarer's loop, and the copies pushed up

*The first two interventions of D114, the owner's choice, applied and re-measured
(`stacks/audit.md` §8). One addition to Baker's API.*

- **Wayfarer's loop.** Its analyses' navigation mesh is rebuilt only when
  `wayfarer::nav_key` changes: every cell's source, the cell size, `climb_max`, `step`, and
  the glide ratio. Edit-to-see for a tweak: 15.9 ms median, 19.9 ms worst, against 174.1 and
  215.1 (D114). A test checks the key changes with the geometry and not with a speed; the
  old flag also kept the analyses on a stale mesh after `climb_max` was tuned in a panel.
- **`Array::tag()`** in Baker: an array's first node if a symbol, else the empty Symbol.
  Six stacks had written it. Reading a value by position needed nothing new: a path indexes
  values (`get<double>("pos.1", 0)`), and a test now says so beside `tag()`; that no stack
  found it is a finding about the docs, not the API. The core is 11,073 lines of 15,000.
- **The base** gained `rng.hpp` (`base::Rng`, xorshift64*, seeded per stream: each stack
  keeps its own stream constant, so every result stays the same), `words.hpp`
  (`base::name`, `base::number` by path or by position, `base::not_found`, `base::truthy`),
  a `stacks_words` header-only target, and `Renderer::box` for the common box.
- **Results.** Copies left: 1 of 31 (Contraption's box helper, which also turns about z and
  draws spheres: a specialisation). The stacks are 112 lines shorter. Every stack's `sim`
  over seeds 1–3 is byte-identical before and after, and the pinned replay hashes hold;
  the 29 stacks tests and Baker's local matrix pass.


## D116. The cache writer keeps flat tables

*Found in a fresh review of the core (2026-10-14).*

- **The cost.** `write_cache` was 4–8 times a text parse of the same file, and a
  development cache miss (`read_cached`, a Config layer with `cache_dir`) is a parse plus a
  write. Under callgrind, half of a write was `malloc` and `free` for `std::unordered_map`
  nodes, and most of the rest hashing: an array's address was looked up three times (seen?,
  placed, written), a string's three, and every child reference once more as it was
  written; every symbol occurrence went through a second map.
- **The change.** One open-addressed table of addresses, with no allocation per entry,
  answers "seen?" once per array or string. As the breadth-first pass meets each array or
  string node it notes the target's offset (or string index) in order, so the write pass
  reads them back in the same order and looks nothing up. Symbols number through a flat
  table indexed by `Symbol::index()`. The file is byte-for-byte the same format.
- **Benchmarks** (`bench_cache`, release, three alternating runs against `adc20b3`, same
  machine; the instructions of one write of the small tree fell from about 138M to 54M):

| | 1.2 MB text, 6.2 MB tree | 4.2 MB text, 21.1 MB tree |
|---|---:|---:|
| `write_cache`, before | 70.8–92.4 ms | 418.7–447.3 ms |
| `write_cache`, after | 27.8–36.2 ms | 85.4–104.0 ms |
| `read_cached`, no cache, before | 91.4–128.2 ms | 545.8–675.8 ms |
| `read_cached`, no cache, after | 52.7–79.2 ms | 178.8–214.3 ms |
| Config layer, `cache_dir`, no cache, before | 114.0–156.9 ms | 697.5–1128.3 ms |
| Config layer, `cache_dir`, no cache, after | 55.5–80.6 ms | 301.0–347.7 ms |

  The text parse and the cache load are untouched (parse 73.8–92.2 MB/s before, 76.5–85.4
  after: noise). What remains of a write is mostly the zero-fill that keeps the file's
  padding the same on every run, which its hash needs.

## D117. Nothing a running call uses is freed before the host's call returns

*Found in a fresh review of the core (2026-10-14); each reproduced under ASan. The owner
chose the deferral, as recommended, over re-checking the receiver after each argument
(which cannot help a native method, whose object is already bound).*

- **The bugs.** D67 deferred removals only while a Scheduler ticks, but a call needs the
  same protection whenever one is running:
  - `{q grow {do {delete q} 1}}` destroyed `q` while its native method's arguments were
    being evaluated, then called the method on it; `{q set x {delete q}}` likewise for
    `set`.
  - `{p get {do {new P} ... nope}}`: `{new}` grew the slot table, and `send` then named the
    class through its stale slot pointer.
  - A `{func}` that redefined itself (directly, or through a native calling `eval`) freed
    the body it was running; a handler that ran `{class}` or `{extend}` on its own class
    freed or moved its `Handler`, whose counters the instrumented path writes after the
    call returns.
  - `Scheduler::tick` kept a slot reference across an `until` predicate, which may spawn
    tasks and move the slots.
  - A Scheduler made with `chunk = 0` took blocks one at a time but freed its chunks as if
    they were 0 blocks long, breaking the Allocator's contract that a free names the size
    allocated.
- **The fix.** While any call runs (`depth > 0`), `{delete}` and `Context::remove` defer as
  they do in a tick, and a redefinition moves the overloads or handlers it replaces aside
  (`retire`) instead of freeing them. `Context::eval` and `Context::run`, the only ways the
  host starts evaluation, settle both when the outermost one returns; during a tick the
  removals still wait for the sync point. A destroyed C++ object still leaves at once
  (D97). `send` looks its slot up again after evaluating arguments, `tick` reads the slot
  again after the predicate, and the Scheduler keeps `chunk` at least 1.
- **What a script author sees.** `{delete q}` takes effect when the host's `eval` returns,
  so later commands in the same `eval` still reach `q`; a function or handler redefined
  while it runs finishes its old body (`language.md`).
- **Tests.** One per bug (`test_object.cpp`, `test_eval.cpp`, `test_tasks.cpp`, and
  `chunk = 0` in `baker_alloc_tests`); against the previous code, five fail under ASan with
  a heap-use-after-free and the allocator test with live bytes left.
- **Cost.** Nothing per nested call: three instructions after the host's outermost call
  (callgrind, `{add 1 2}` through `Context::eval`, 6 instructions a call more in all).
  `bench_call` pinned to one CPU, six alternating runs against `adc20b3`: `{add 1 2}`
  37.8–40.7 ns before, 38.0–39.0 after. `bench_tasks`: tick with 10,000 asleep 10.3–10.5 ns
  before and 9.7–10.5 after; resume 16.4–16.7 and 16.2–17.0.

## D118. Two scratch-layer fixes

*Found in a fresh review of the core (2026-10-14). The owner chose, as recommended, that a
set while scratch is disabled waits rather than being refused.*

- **A shorter scratch value hid nothing.** With `(size 1 2 3)` in a layer and
  `set(Path("size"), {5})`, `get<int>("size.2")` returned the layers' 3: the scratch lookup
  answered "no entry" for an element past the end of a scratch value, and `get` fell back
  to the tree of the last merge. `Impl::current`, which write-back and the journal use,
  already said "past the end" in its own copy of the lookup. The lookup now answers none,
  found, or past the end (`Config::ScratchHit`); `get` reports the third as NotFound, or
  returns its fallback, and `current` uses the same lookup.
- **A set with scratch disabled reached the tweaks.** `retune`, run by both `set` paths,
  pushed the value into bound tweaks although `get` did not see it, and the next full
  merge silently put the tweaks back. It now waits while scratch is disabled; the merge
  that enables scratch retunes them.
- **Tests** (`test_m3b.cpp`): both fail against the previous code.

## D119. The cache hashes its header; a nested shape is checked once

*Found in a fresh review of the core (2026-10-14).*

- **The cache's header was outside its hash.** The hash covered the bytes after the
  64-byte header, and the loader checked only that `root` and `meta` were aligned offsets
  among the arrays. A cache of `(a 1) (b 2)` with `root` changed from 64 to 128 loaded,
  and returned `(a 1)` as the tree. The hash now covers the whole file, the header read with
  its own hash field as 0, and `root` must be 64, where the writer always puts it. Format
  version 3: a version 2 cache is refused by version, and rebuilt by `read_cached` and a
  Config's `cache_dir` as any stale cache is. The test flips each header word from the
  hash on; against the previous code three of them loaded. `read_cache` hashes 64 bytes
  more: `bench_cache`'s load rows, three alternating runs against `adc20b3`, read
  3.75–4.87 ms before and 3.20–4.84 after (noise).
- **`baker::shape` reported a nested shape's errors twice.** A field of shape type, or a
  `(list S)`, checks its arrays against `S`; the walk then met the same arrays again and,
  since an unkeyed shape governs every array tagged with its name, checked them by tag.
  `(boss (attacks (attack (dmg 50))))` reported the range error twice through every Config
  load. The checker now remembers the arrays it checked as a field's value and does not
  check them again by tag (a loose `(attack ...)` still is). Test in `test_shape.cpp`.

## D120. A polling reload allocates nothing and hashes at memory speed; cleanups

*From the same review (2026-10-14).*

- **Polling.** A host that polls `reload()` each frame reads and hashes every file a layer
  was built from. Each read allocated a `Str` for the text and another for the path
  (`read_disk_file` copied it to terminate it for `fopen`), and the hash was FNV-1a, a
  byte at a time. The text now goes into a buffer the Config keeps, from its allocator; the
  path is terminated on the stack (`detail::CPath`, which the cache loader already did by
  hand); and the hash is the cache's four-lane `content_hash`. A test checks an unchanged
  poll allocates nothing. `bench_cache` has a new row, a reload finding the large file
  unchanged; three alternating runs against `adc20b3`, pinned to one CPU:

| | 1.2 MB text | 4.2 MB text |
|---|---:|---:|
| Config reload, unchanged large file, before | 1.88–2.36 ms | 7.63–8.85 ms |
| Config reload, unchanged large file, after | 0.19–0.21 ms | 1.00–1.25 ms |

  The one-line file's row is the same (3.88–4.81 µs before, 3.86–4.31 after): it is the
  system calls.
- **Tweak metadata** (`range()`, `step()`, `desc()`, `note()`, `kind()`, `group()`, and
  the trailing-metadata test every bind and reload makes) interned its keys on each call,
  which hashes the text and locks the global symbol table; a tuning panel calls them for
  every tweak every frame. The keys are interned once.
- **Number classification** rejects an atom that cannot be a number on its first
  character, so a symbol is not scanned twice. A parse runs 2.7% fewer instructions
  (callgrind, 1.2 MB, 377.0M to 366.8M); the time is within noise (`bench_parse`, pinned,
  83.3–88.8 MB/s before, 84.1–87.0 after).
- **`write_dta`'s pretty printer** formatted each array twice, the first time into a
  temporary string, to learn whether it fit on a line. It now writes the compact form in
  place and takes it back if it does not fit.
- **Dead code.** `Node::aux()` and `set_aux()`, reserved for D5's in-node cache, which D44
  decided not to build; `Array::set_source`, which nothing called; `ScriptClass`'s
  `native`, `file` and `line`, and `TaskSlot::name` (spawn's name is still used in its
  error), written and never read.
- **Duplicates.** `quoted(Symbol)` was written six times (data, shape, and four script
  files): now once, in `data/text.hpp`. The script files share `BAKER_COLD`, `fail()`,
  `kUnset`, `is_unset()` and `sym_this()` through `internal.hpp`, where each had two or
  three copies. Config's `read_into` was parse.cpp's `read_with`, and `Hash<Str>` was
  FNV-1a written again, with a digit missing from its offset basis.
- **Tools.** `local_matrix.sh` configured a build directory only when it did not exist,
  so a changed flag or a failed first configure (the harness, before its X11 headers were
  installed) was never retried; it now configures every time and counts CMake errors.
  The header-alone units are written with `file(CONFIGURE)`, so a reconfigure no longer
  recompiles all 27. `count_lines.py` counts `std_script.hpp` with baker::script.
- **Benchmarks** of the rest, against `adc20b3`, pinned: `find<float>` 15.3–15.4 ns before
  and 12.1–12.7 after; `get<int>(Path)` 27.6–28.4 and 29.6–33.3, which callgrind counts at
  the same instructions (58,598,051 before, 58,597,781 after, in a small driver): code
  layout. `{add 1 2}` 37.8–39.7 and 38.5–40.6.
- **Size:** the core is 11,128 lines (11,073 before D116; the fixes of D116–D119 added
  more than this removed).

## D121. `layer_effect` and `diff` without their quadratic loops

*From the review of D116–D120 (2026-10-14), which left these for later; the owner asked
for them next.*

- **`layer_effect`** asked, for each leaf, every layer above the one examined whether it
  supplied the path, each answer a resolve down the whole stack: O(L²) resolves a leaf.
  The topmost other supplier, which it already found for `below_source`, answers the same
  question: the leaf is shadowed if that layer is above it (or scratch has it). It also
  found its own layer by name for each leaf's source, and interned "scratch" for each.
- **`diff`** matched each leaf of one layer against every leaf of the other, both ways:
  O(n·m) path comparisons. It now indexes the second layer's leaves by path once.
- **Test.** The three-layer test also checks a disabled layer (shadows nothing), a layer
  two above (shadows), and scratch (shadows); it passes against the previous code too,
  as an equivalence should.
- **Benchmark** (a driver of four layers of 2,000 leaves, examining the second, release,
  pinned; the harness's Layers panel calls `layer_effect` each frame while open):
  `layer_effect` 1.29–1.36 ms before and 1.10–1.17 after, and `diff` 3.46–3.54 ms and
  1.25–1.32. With four layers `layer_effect` is mostly its one re-merge of the stack
  without the layer, so the quadratic term mattered little here; it grows with layers.

## D122. `config.cpp` split by subsystem

*From the review of D116–D120 (2026-10-14), which left it for later; the owner asked for
it next. A move: no behavior changes.*

- **Why.** At 1,750 lines `config.cpp` held four subsystems: layers, merging and scratch;
  the reload history and loop statistics; write-back planning (one 180-line function) with
  `promote`; and the layer tools. Its private types were in an anonymous namespace, so no
  other file could take any of it.
- **Now.**
  - `config_impl.hpp` (internal) holds the shared records (`LayerState`, `ScratchEntry`,
    the loop table) and `Config::Impl`: its state, the small accessors inline, the rest
    declared. Its helpers are in `baker::config_internal`, not in baker's own names.
  - `config.cpp` (751 lines) has layers, reading and reload, the merge, scratch and the
    edit journal, and snapshots.
  - `config_loop.cpp` (172) has the reload history and loop statistics.
  - `config_write.cpp` (540) has write-back planning and writing, `promote`,
    `layer_effect` and `diff`, with the default disk writer.

  Each helper lives in the file that uses it, and only the six shared by two files are
  declared in the header.
- **One cost, found and removed.** With its types in an anonymous namespace, every `Impl`
  method taking one had internal linkage, so GCC could see all of `retune`'s callers and
  inlined it into `set()`'s fast path. Split out, it became a call: 17 instructions a
  `set` (callgrind, 200,000 sets). It is now `always_inline` (`__forceinline` on MSVC),
  which brings `set` to 115.6M instructions against 116.8M before the split. Pinned
  timings, three alternating runs against `4404b48`: `Config set, a scalar in scratch`
  87.6–89.1 ns before and 89.8–91.7 after (fewer instructions, so code layout); `reload`
  of an unchanged small file 3.93–3.97 µs and 3.94–4.01; `mark_visible` 0.30–0.31 ns and
  0.30.
- **Size:** the core is 11,181 lines (11,128 before): the header's declarations and three
  files' preambles.

## D123. The review's remaining findings, recorded as open gaps

*The owner's choice (2026-10-14): fix the config split and `layer_effect` now (D121,
D122), and record the rest for later.*

The review that produced D116–D122 also reported findings it did not act on. They are now
gaps 13–30 in `.claude/skills/acton-review/references/baker-gaps.md`, each with its path
class and whether anything measured it. Most are only read from the code, so each needs
measuring before it is fixed (the skill's step 5). The ones on a hot path come first:
- gap 13, two allocations per C++-to-script call;
- gap 14, the method walk once any `{class}` exists;
- gap 15, a refcounted copy per `const Array&` argument.

Gap 18 is a load-path deviation from D61 (layer records on the default allocator).
Gap 26 needs the owner's decision: whether `('' 1)` is tagged.

## D124. Gaps 13–18: the hot-path batch

*The owner asked for the next batch of D123's gaps (2026-10-14). Each was measured before it
was changed. `bench_call` gained three rows: `handle()` to a script handler, `{bob grow
0}` with a `{class}` for another type loaded, and `{len (1 2 3)}`. The numbers below are
three alternating runs against `fd99ac7`, pinned to one CPU, with the same bench file.*

- **Gap 13, a C++-to-script call allocated twice.** `Context::call` built a `Vec` and
  then an array, for every call from C++ and for every `handle()` that falls through to a
  script handler or method. The Context now keeps one command array for each size below
  8. A call reuses it when the cache holds its only count (a nested call, or a `{func}`
  that kept its own command, holds another), sets it, and evaluates it. Afterwards it empties
  the slots so the array keeps nothing alive. It does so only if the script kept nothing:
  an array the script kept is dropped from the cache, never reused. Tests:
  - a warm C++ call into a script handler or a native method allocates nothing;
  - a `{func}` built through `Context::call` keeps its body through later calls of the
    same size. With the reuse check removed, this test fails.

  `handle()` to a script handler went from 131.3–132.1 ns and 2 allocations to 113.3–113.5
  ns and none. `ctx.call(bob, "grow", 1.5f)` went from 183.4–190.6 ns to 153.1–157.2 ns.
- **Gap 14, the method walk.** `send()` now tests the `handled_names` bit before it
  walks a type and its bases for script classes, as `detail::scripted` already did; both
  share `Impl::handled`. With an unrelated `{class}` loaded, `{bob grow 0}` went from
  83.7–84.6 ns to 81.5–83.0, the same as with no class (79.6–81.2). The C++ `handle()` path
  runs the same instructions as before (31,077,378 and 31,077,434 for 200,000 calls).
- **Gap 15, `Args::array`.** A literal array argument, or one an overload set already
  evaluated, is held by the command or `kept` for the whole call. It is now returned as is,
  without a copy, two refcount updates, and a push. `{len (1 2 3)}` went from 28.5–29.7 ns
  to 19.7–20.1. The binding test also covers a variable, a command's result, and two
  non-array arguments.
- **Gap 16, debug builds.** Each argument's index is a compile-time constant
  (`std::integral_constant`). Before, a debug build looped over the parameters per argument
  per call. At `-O0` a three-argument call runs 117 fewer instructions (402.1M against
  390.4M for 100,000 calls); release builds folded it already.
- **Gap 17, `Slot`.** Pointers first: 32 bytes instead of 40, pinned by a `static_assert`.
  The cold rows that reach many objects moved: one of 2,000 objects 90.4–93.8 ns before,
  86.5–87.9 after. 64 types with one method went from 138.0–143.2 to 126.2–129.0, and with
  32 methods from 207.0–209.8 to 196.3–199.6.
- **Gap 18, the Config's records.** Measured first: in `baker_alloc_tests`' Config
  scenario, 222 allocations went to the default allocator and 21 to the Config's.
  Stacks of each showed the layer records were about ten. The rest is what D61 assigns to
  the default allocator:
  - merged trees (solo blocks, D64);
  - merge's and the parser's temporaries;
  - sessions and source maps.

  The records the Config makes now take its allocator: each `LayerState`'s strings and
  deps, and the deps a read builds. The scenario now reads 215 and 28; the test prints
  both. Whether a Config's merged trees should leave the default allocator is an
  ownership question, recorded as gap 31.
- **Unchanged:** every other `bench_call` row is within noise. `{add1 1}` read 80.7–81.8 ns
  before and 82.2–85.2 after, at the same instructions (220,503,011 and 220,503,076 for
  200,000 calls): code layout.

## D125. Gap 26: `''` is no tag, everywhere

*The owner's decision (2026-10-14), as recommended: D115's meaning.*

- **The disagreement.** `Array::tag()` (D115) returned the empty Symbol for `('' 1)`, as
  for an untagged array. But four other places tagged it: lookup (`try_find('')` found it
  through the tag run, and sorted lookup through its own `tag_of`), merge (it matched two
  such arrays by key and merged their children), the Config's leaves, and the cache's tag
  run.
- **Now.** `Array::tag()` is the one meaning, with `detail::tag_of(const Node&)` for nodes:
  - the tag run keys `''` as no tag;
  - `find_sorted('')` finds nothing;
  - merge treats `('' ...)` as a value;
  - the Config's `is_tagged` and the shape checker's walk follow suit.

  The cache's tag run writes no tag for `''`. Its format is version 4, so a version-3
  cache is rebuilt.
- **Data.** Nothing in the tests, examples, stacks or docs used `''` as a key.
- **Test** (`test_lookup.cpp`): linear and sorted lookup, merge, and a cache round trip.
  Against the previous code it fails four times.

## D126. Gaps 19–21: the load-path batch; a leak the census found

*The owner chose this batch (2026-10-14). Each gap was measured before it was changed:
callgrind and pinned timings in small drivers, against the previous build.*

- **Gap 19, the lexer.** It skipped `;` comments, scanned strings for the closing quote,
  and counted newlines inside strings and quoted symbols, all a byte at a time. Neither
  escape (`\n`, `\q`) contains a quote, so a string ends at its first one: `find`
  (memchr) now scans both strings and comments, and `std::count` counts newlines. A string
  is unescaped only if it holds a backslash at all.
  - On `bench_parse`'s text, with short strings, it is 0.36% fewer instructions and no
    timing change (132–137 MB/s before and after).
  - On text of long descriptions and comment headers, it is 26% fewer instructions
    (90.8M to 67.3M for three parses of 1.2 MB) and 450–460 MB/s to 610–623.
- **Gap 20, the shape checker.** It found each tagged array's shape by scanning every
  shape, gave every instance its own `seen` vector, and kept D119's checked-nested set
  as a hash map, a node per nested value. Now:
  - shapes are found by `Symbol::index()`;
  - `seen` is one stack with a mark per instance;
  - the nested set is a stack, emptied as the walk leaves the instance that filled it.

  Checking 20,000 units against 21 shapes went from 12.7–13.1 ms and 140,014 allocations
  to 3.3–3.5 ms and 12. A test checks that 1,000 instances allocate what 10 do.
- **Gap 21, cache freshness.** Each check printed every session define's body to text to
  hash it: 92% of a check. Bodies are now hashed from their nodes: type, value, a
  symbol's precomputed hash, and a string's content hash. That is stable across
  processes and allocates nothing. A check with 200 session macros went from 98.6–101.5
  µs to 34.6–35.0. A cache written by the previous code reads as stale, once, and is
  rebuilt.
  - Test: a define's body decides freshness by structure. An equal body from another
    parse is fresh; a changed number, a string for a symbol, or added nesting is stale.
- **A leak (found by the allocation census for gap 31).** `Context::remove` on an object
  `{new}` made, outside any call or tick, removed it but never destroyed it. A removal
  deferred to the end of a call or a tick did destroy it. Both now go through
  `Impl::remove_now`. The test fails against the previous code.
- **Unchanged:** `bench_lookup`'s rows. `find<float>` read 15.2–16.7 ns before and
  17.0–17.4 after, at the same instructions (37,251,771 and 37,251,931 for 200,000
  finds): code layout.

## D127. Gap 31: fix merge, keep merged trees solo

*The owner's decision (2026-10-14) on `dev/allocation-review.md`: option A, as
recommended. D64 stands: arrays built after a load are solo blocks.*

- **Why not an arena.** The review measured where a merge's cost lay. About 7 of every 9
  arrays it rebuilt were copies of a layer's own array, and 77% of its allocations were
  temporaries. An arena per merge would have kept both kinds of waste. It would also have
  brought new retention: one held 80 B leaf would pin about 1.1 MB of merged and layer
  arenas. And `Array::set` through `root()` would have kept every replaced node.
- **What changed.**
  - When a rebuilt array's nodes come out identical to the incoming array's, node for
    node, the merge shares that array instead of building a copy. The shared array keeps
    its own location, the layer's.
  - The merge's temporaries live in a `detail::MergeScratch`: per depth of the
    recursion, a buffer for the array being rebuilt and an open-addressed index of a wide
    array's tags. The index is sized by the keys being merged, not by the symbol table. It
    replaces D75's `unordered_map`, one node per key. Levels are owned one by one, so a
    deeper level never moves a buffer an outer one uses.
  - The Overwrite branch reorders `dst` in place rather than swapping it with a buffer
    that may not share its allocator.
  - The Config keeps one scratch on its allocator for every merge it makes: its stack,
    scratch overlays, and `layer_effect`. `baker::merge` makes its own, and `#merge` in a
    load makes one.
  - `merge` reserves its result once rather than growing it.
- **Measured** (a driver of three file layers of about 36 KB each, one full merge,
  release, pinned; before is `6298f1a`):

| | before | after |
|---|---:|---:|
| default-allocator allocations | 9,067 | 8 |
| the merged tree's own bytes | 176,744 | 12,184 |
| time | 365–374 µs | 152–154 µs |

  A Config whose top layer overrides whole sections now allocates 6 times a merge
  whatever its size: 20 sections or 800.
- **Tests** (`test_config.cpp`):
  - a layer that sets a whole section shares it;
  - a merge allocates the same for 200 sections as for 20;
  - wide arrays at two depths at once merge as one key at a time does.

## D128. Source maps are flat, and a Config's are its own

*The review's largest finding after merge (`dev/allocation-review.md`), the owner's
choice.*

- **The cost.** Edit mode, the default outside shipping, records for every array where it
  and each node are written. Each array cost two allocations on the default allocator: a
  hash-map node, and a vector of its spans. A 36 KB layer made about 5,000. The Config keeps
  one map per layer, which made it the largest thing a Config held off its own allocator.
- **Now.**
  - Every array's spans lie in one run. An entry (the array kept alive, its extent, and
    where its spans are in the run) is found through an open-addressed index by address.
  - Recording an array allocates nothing of its own. When a file's text is recorded, the
    run, the entries and the index are reserved for it at config's density: a node every
    4 to 5 bytes and an array every 14. A denser file costs one doubling, not a regrowth
    per array.
  - Recording an array again (a `#merge` rebuild) replaces its entry.
  - `SourceMap(const Allocator&)` is new. The Config passes its allocator for the maps it
    makes, which D61 wanted: records an owner keeps come from its allocator. A host's map
    stays on the default allocator.
- **Measured** (an edit-mode read of a 36,306-byte layer, release, pinned, three
  alternating runs against `e11054b`):
  - default-allocator allocations: 5,084 before, 49 after;
  - bytes live with the tree: 896,795 before, 892,915 after;
  - time: 536–572 µs before, 422–437 µs after.

  In `baker_alloc_tests`' Config scenario, the default allocator's count went from 172 to
  136 and the Config's from 38 to 62. `reloads free the trees they replace` now takes its
  steady reading at version 10: the map's copy of the text is the Config's now, and
  versions 2 to 9 are a character shorter.

## D129. A Config's sessions are its own, and a merge copies none it need not

*From `dev/allocation-review.md`, the owner's choice.*

- **Every merge copied a session.** `refresh_read` copied the top file layer's whole
  `LoadSession` state into `read_session()`'s, at every commit: every define (a map node
  each), pack, origin and warning. A layer's session is read-only once built, so the copy
  is now skipped when the top layer's session is the one copied last time. The Config
  holds an `Rc` to that session, so its address cannot be reused by another. The base
  session is the host's to change, so it is still copied each time it is the top.
- **Sessions on their owner's allocator.** `LoadSession(const Allocator&)` is new: its
  state (defines, packs, origins, the record for a cache) comes from that allocator. A
  copy keeps its source's allocator; an assignment keeps the target's, as containers do.
  The Config's base session and `read_session()` copy are on its allocator, and so,
  through copies, is every layer's session.
- **Measured.** One full merge of three file layers, the bottom one defining 200 macros,
  release, pinned: 209 default-allocator allocations before and 7 after; 160 µs and 153
  µs. `baker_alloc_tests`' Config scenario: the default allocator's count went from 136 to
  123, and the Config's from 62 to 74.
- **Test** (`test_m3b.cpp`): a merge allocates the same with 100 macros as with 5. Against
  the previous code it read 107 and 12.

## D130. A Context's callables and handlers are its own

*From `dev/allocation-review.md`, the owner's choice. Closes the review's D61
inconsistency in baker::script.*

- **The inconsistency.** D61 says function records and classes are the owner's. But a
  function's callable is a `detail::Shared`, classed in D61 as a header type, and so came
  from the default allocator. That covered every native's callable and every `{func}`'s
  `ScriptBody` with its parameter lists. `ScriptClass` took no allocator, so neither did
  its handlers and their bodies.
- **Now.**
  - `Shared::make_in(alloc, ...)` records its allocator, and `Shared::make` is
    `make_in(default_allocator())`.
  - `make_overload` takes an allocator; `Context::func` passes the Context's, through the
    new `Context::allocator()`.
  - `{func}` bodies, `ScriptBody`'s vectors, `ScriptClass`'s handler vector and each
    handler's body take the Context's allocator.
  - Type records stay process-wide, on the default allocator, as D61 says.
- **Measured** (`baker_alloc_tests`' Context scenario, which now prints both): the
  default allocator's count went from 121 to 80, and the Context's from 132 to 173. What
  remains on the default allocator is what D61 assigns there: `eval(text)`'s parse
  temporaries, error records, type records and the symbol table. Nothing on a call
  changed: `Shared::get()` is the same load.

## D131. A load's first block is sized from its text, and a layer read copies its text once

*From `dev/allocation-review.md`, the owner's choice ("arena sizing and text copies").
It also corrects D61's and D64's text, and documents `eval(text)`'s lifetime.*

- **The first block.** A load's arena started with a block of four bytes per byte of
  text. Real trees run from about half their text (string-heavy) to eight times it
  (bracket-heavy), so that block either wasted most of itself or doubled into a second and
  third. A 102 KB tree-heavy text took three blocks and used 51% of 1.23 MB.
  - **Now:** `first_block` counts arrays, tokens, strings and characters in one pass over
    the first 16 KB, then scales the count to the whole text. A text of 16 KB or less keeps
    the old 4×, where the pass would cost more than the slack saves.
  - **After a full first block,** the next block is the same size, then sizes double as
    before. The first block was meant to hold the whole load, so doubling it on a small
    miss overshoots.
  - A pass over the whole text cost 17% of parse speed (134 → 111 MB/s), which is why
    only 16 KB is sampled.
- **Measured.** Parses in a counting driver:

  | Text | Blocks before | Blocks now | Bytes before | Bytes now | Used before | Used now |
  |---|---:|---:|---:|---:|---:|---:|
  | dense, 102 KB | 3 | 2 | 1,224,608 | 700,136 | 51% | 88% |
  | sparse | 2 | 2 | 408,144 | 137,080 | 34% | ~101% |

  - Sparse's ~101% is its two blocks counted against the estimate.
  - Most corpus files now load in one block.
- **`bench_parse`** (4.2 MB corpus), pinned to one CPU, alternating runs:
  - Before: 117.9, 116.5, 115.9 MB/s. Now: 125.5, 127.7, 128.5 MB/s. 43 allocations both.
  - An earlier session read the other way (137 vs 131), so this is noise.
  - Callgrind: 4,114.9M instructions before, 4,117.4M now (+0.06%, the sampling pass).
- **Text copies.** A Config's layer reader read each file into a fresh `Str`, hashed it
  for the layer's deps, and passed it to the parser, which copied it again.
  - Now the reader reads into the Config's `scan` buffer (`changed()`'s, reused), so a
    file read allocates only the parser's copy.
  - The reader returns before the parser reads an include through it again, so the buffer
    is never in use twice.
  - A dep's path is now on the Config's allocator, like the rest of its layer records.
  - `baker_alloc_tests`' Config scenario: the default allocator's count went from 123
    to 120.
  - **Left:** a cache read through a custom reader still copies the cache from the
    reader's `Str` into the arena that holds it. Without a size up front, the arena
    cannot be the sink. Custom readers (packs, memory) are the rare path.
- **Corrections to earlier entries.**
  - **D61** lists snapshots among the default allocator's results. A `ConfigSnapshot` is
    built on its Config's allocator and keeps it (D62), as D61's own note on copies
    implies.
  - **D64** says blocks grow from 4 KB. The minimum block is 512 B (`kMinBlock`). The
    first block is sized for the load: 4× the text in D64, and `first_block` since this
    entry.
- **`eval(text)`'s lifetime.** `Context::eval(text)` builds its tree in an arena from the
  Context's allocator. A value kept from it, or a `{func}` the text defined, keeps that
  arena alive, so the allocator must outlive it as well as the Context. D61's contract
  already covered this; `script.hpp` now says so where `eval` is declared.

## D132. The write-back batch: one merge a write, one session a promote, a write stage

*Gaps 22–25, the owner's choice of batch. Adds `ReloadRecord::write_ns`, which deviates
from spec §4's record; the owner chose a new stage over folding the write into another.*

- **Gap 22: write-back merged twice.**
  - To decide whether scratch should keep a written value, `write_into` merged the whole
    stack, and then `commit` merged again.
  - Now `supplier` names the layer whose values win, and that layer's own tree is compared
    with the values written.
    - Overwrite merging makes this the merged value: the highest enabled layer with values
      at the path supplies them.
    - Scratch's other entries cannot cover the path: the entry with the same key is the one
      being decided.
  - A kept entry now keeps its place in scratch rather than moving to the end.
  - `source_in` is computed once.
- **Gap 23: session copies.** `refresh_read`'s half was closed by D129. `promote` copied
  the session below the target for every leaf; `read()` copies what it is given, so one
  copy before the loop serves them all.
- **Gap 24: the write's time.** `write_back` started its record before the writer ran, but
  counted the write in no stage, so the stages did not tile the record as `finish()`
  promises. `promote` wrote before starting its record.
  - Both now start the record first and time the write as `write_ns`, between `scan_ns`
    and `read_ns`.
  - The reread, timing and parse-error block written three times (reload, `write_back`,
    `promote`) is one helper, `reread_stages`.
  - The ImGui harness's loop panel draws seven stages.
- **Gap 25: quadratic leftovers.**
  - `collect_leaves` kept a vector of seen tags per array and searched it for every child.
    Now a child is skipped unless `find()` returns it, a scan of the tag run (D77); past 32
    children, a set.
  - `restore` searched the scratch for every journal entry. Now each scratch entry finds
    its own edit, and the journal is checked against that sorted list.
- **Measured.** A driver with four in-memory layers (edit mode): base 280 sections, mid
  280, top 187, an empty local. Each section is six keys. Release, pinned, three
  alternating runs. Before is D131.

  | | before | after |
  |---|---:|---:|
  | `write_back` of one value in mid, best of 30 | 923–962 µs | 793–822 µs |
  | its median | 1,080–1,089 µs | 864–892 µs |
  | its default allocations | 151 | 143 |
  | `promote` of 327 scratch leaves into local | 18.3–19.0 ms | 18.0–18.5 ms |
  | its default allocations | 24,896 | 24,240 |

  - Callgrind over the driver: `write_back` 341.6M → 307.3M instructions for 30 calls
    (−10%), of which `merged()` 80.5M → 46.4M.
  - `promote`'s instructions are unchanged (210.7M → 210.3M): it re-reads the target once
    per leaf to plan the next, and that dominates. Planning every leaf against one read is
    a larger change, and promote is a tool path.
- **Tests.**
  - A write-back's and a promote's stages tile their records, with `write_ns` timed.
  - A promote takes each tag's first child, at 4 and at 40 children.
  - The second test found gap 32: the Config reads a duplicated tag as the later child
    (merge folds it in), but `promote` writes the first. That predates this entry, and
    which meaning is right is the owner's call.

## D133. The reference prints a method's status

*Gap 29. The owner chose to print a method's status rather than drop its replacement.*

- **Before.** `MethodRef::status` stored a method's `Status` and replacement, but the
  reference printed status for functions only. A deprecated method looked like any other,
  and its replacement was stored for nothing.
- **Now.** One `status_text` gives a status as the reference shows it, for functions and
  methods alike.
  - A method's first overload line carries it in parentheses, before the doc. For
    example: ``- `{<Robot> walk int32}` → int32 *(deprecated: use `stride`)*: Walks `n`
    steps.``
  - Functions print as before (`*Native, deprecated: use `jump`.*`).
- **Checks are unchanged.** They demand only `Stub`, for functions and methods alike, as
  D-entries before this one decided.
- `docs/script-reference.md` is unchanged: no built-in method has a status. A test binds
  a type with a stub, a provisional and a deprecated method.

## D134. Promote writes what the Config reads

*Gap 32, found writing D132's test. The owner chose the Config's reading over refusing
duplicates or keeping `find()`'s.*

- **The mismatch.** A layer can repeat a tag. Merge folds the repeat into the first child,
  the later values winning, so the Config reads `(k 1) (k 9)` as 9. `promote` took each
  tag's first child, as `find()` sees one array alone, and wrote 1. Worse, a repeated
  section's own keys were never visited: `(sec (a 1)) (sec (b 2))` promoted `sec.a` and
  lost `sec.b`.
- **Now.**
  - `promote` visits every tagged child, a repeated tag's included, and keeps each path
    once (`collect_paths`).
  - It reads each path's values from the stack as the Config would see it with the source
    on top: the enabled layers below the source, then the source, merged. Scratch is
    merged over every enabled layer.
  - After a promote, the target alone reads as the stack did. The test disables the
    source and checks this.
- **Cost.** One merge of the layers up to the source per promote, a tool path; the merge
  reuses the Config's scratch (D127). In D132's driver (327 scratch leaves into local),
  release, pinned, three alternating runs:
  - time: 17.5–17.8 ms before, 18.2–18.3 ms after (+2.5%);
  - default allocations: 24,240 before, 24,577 after (+337: the merge's few, and one key
    a path in the set of paths seen).
- **Left as they are.** `layer_effect` and `diff` still list each tag's first child, as
  `find()` does. They describe a layer's own text rather than move values, and nothing
  reported a problem with them.

## D135. Three latent desyncs

*Gap 27, from the 2026-10-14 review (D116–D122).*

- **An overwriting merge left spans behind.**
  - `merge_into`'s Overwrite branch rebuilt `dst` (the incoming values, then the tagged
    children kept) but not the spans beside it. Each span stayed at its old index, now
    under another node.
  - No caller combined the two yet: `#merge` fills missing keys, and Config merges keep no
    spans. But write-back reads spans by index, so the first such caller would have
    written into the wrong bytes.
  - Now the tagged children's spans are set aside with them, and the incoming values take
    their own spans from the map.
- **An included file's nodes counted twice toward `max_nodes`.** The included file's
  parser counted every node it made. The including parser then spliced the file's
  top-level nodes in with `push`, which counted them again. A file of 10 nodes included
  under `max_nodes = 10` failed. Splicing now passes `counted`.
- **A source map read again kept the old text.** `record_text` kept the first text of a
  file it had seen, so a map reused across reads of a changed file returned text that no
  longer matched its spans. It now replaces it.
- **Tests:** one each, and each fails against the previous code.

## D136. Six duplications, each now written once

*Gap 28, from the 2026-10-14 review.*

Each pair below did the same thing in two places, so a fix in one could miss the other.
Behavior is unchanged and the existing tests cover each site.

- **Path tokenizing.** `Path::parse` and `Path::matches` read a path's segments alike:
  quotes, separators, a trailing separator, and an all-digits name as an index. Both now
  read through `next_segment` and `is_index`.
- **`LoadSession`'s options.** Its copy and move constructors and assignments each copied
  the same five public fields. They now call `copy_options`, so a sixth option is added
  in one place.
- **Pack and origin registration.** The Parser registered a pack or an origin its own way,
  and a cache's replay did it through `SessionAccess`. Both now use
  `SessionAccess::add_pack` and `add_origin`; `add_pack` returns the pack's index, which
  the Parser keeps.
- **Locale-neutral `%g`.** `format_double_fallback` and `float_to_decimal_double` each
  printed with `%g` and replaced the C locale's decimal point by hand. Both now print
  through `print_g`.
- **The arity message.** "`'f' takes 2 arguments, given 3`" was assembled at five sites:
  calls, script handlers, `get` and `set`, and two checks. `arity_message` builds it.
- **An array's node offset.** The cache re-derived where an array's nodes start, which
  must match `Array`'s layout. It now uses the new `Array::nodes_offset`, which `bytes()`
  and `data()` share.

## D137. Build time: doctest and the counter compiled once, bench_call in parts

*Gap 30, from the 2026-10-14 review.*

- **doctest's implementation** was compiled twice, about 5 s each at `-O3`: once in
  `doctest_main.cpp` and once in `tests/alloc/main.cpp`, whose `main` installs the
  allocator first.
  - It is now compiled once, in `tests/doctest_impl.cpp` (the `baker_doctest` library).
  - `doctest_main.cpp` and the allocation tests' main are mains only, about 0.4 s each.
  - The stacks' doctest library builds both files.
- **The allocation counter** was compiled into seven targets. It is now one OBJECT library,
  `baker_alloc_counter`, which every test and benchmark links.
- **`bench_call.cpp`** was one 23.9 s unit: binding a type costs the compiler about
  0.17 s, and the benchmark binds 128 of them (64 types with 32 methods, 64 with one).
  - Making the method count a run-time value would not have helped: the two sets still
    need distinct types.
  - The types now live in `bench_call_wide.hpp` and build as four generated units of 32,
    in parallel, each about 5.7 s. `bench_call.cpp` itself takes 3.2 s.
  - The objects are made in the same order as before, so the benchmark is unchanged.
- **Measured.** Clean release builds with tests and benchmarks, against D136, on this
  four-core machine:

  | | before | after |
  |---|---:|---:|
  | longest single step | 22.5–25.5 s | 7.0 s |
  | compile time summed over every step (from ninja's log, per build) | 194 s | 188 s |
  | `ninja -j4`, wall | 49.8–50.8 s | 46.0–48.6 s |

  - At four cores the build is bound by the total, so wall time moves little.
  - With more cores the longest step bounds it, and that step is 3.6 times shorter.
  - Splitting `bench_call` repeats its headers four times (about +2.4 s of CPU). The
    doctest and counter changes save more than that.

## D138. Content updates: the owner's decisions on the research's findings

*From `dev/background/content-updates.md`, which measured twelve scenarios of title
updates, DLC, live ops and mods in development and shipping builds, and the delta cost of
text against `.datb`. The owner decided each finding (gaps 33–43). Each item below becomes
its own D-entry, with tests and numbers, when it is built.*

- **Gap 33, stale caches in shipping: check, fall back to the text.**
  - Shipping compares the hashes a cache recorded for the macros its load began with
    (`before`, D72) against the session's. This needs no file I/O.
  - On a mismatch it reads the text if present. Otherwise it refuses with a located error
    naming the package.
  - This changes D70 Q2 ("shipping trusts the cache") for this one check.
- **Gap 34, delta patching: a delta-friendly cache format.**
  - The `.datb` layout changes so that an edit stays local. Candidates are:
    - source-order layout;
    - offsets relative to the referencing node or the section;
    - one table of contents and the symbol table at the end;
    - no padding that moves.
  - It is measured against this note's xdelta and 1 MB chunk tables.
  - This is a format change (`kVersion` 5) and a design note before code (CLAUDE.md).
- **Gap 35, removal: a delete marker.** A reserved form in a layer removes that key or tagged
  child from the layers below during the Config merge, and shows in `diff` and
  `layer_effect`. The spelling is decided in its design note.
- **Gap 37, order: `insert_layer` before or after a named layer.** It re-reads the layers
  above it, as `remove_layer` does.
- **Gap 36, value lists: keyed tables are the convention.** The cookbook documents keyed
  children for anything packages extend. There is no new syntax.
- **Gap 38, shapes: harder packs' shapes hold.** A softer pack may add shapes and fields. A
  range it widens, or a type it changes, on a harder pack's shape is reported, and the
  harder declaration stays in force. This extends D80's one-way rule to D84's shapes.
- **Gap 39, script lifecycle: script leaves with its layer.** The Context records which layer
  defined each function and handler. Removing the layer removes them, and an earlier
  definition (native, or a lower layer's) comes back.
- **Gaps 40–43, all taken:**
  - cache names from the normalized path, with "no cache for X" said on fallback;
  - a located warning when a pack's `requires` is not loaded;
  - versions in pack declarations and in what they require, checked like `requires`;
  - a session option that refuses caches (text-only reads), plus a documented rule that
    mod loaders accept text only.
- **Order of work:**
  1. Correctness first: gap 33.
  2. The small API and convention items: 40, 41, 43, 36.
  3. Layer semantics: 35, 37, 38, 39.
  4. Versions: 42.
  5. The cache format redesign last, because it is the largest: 34.

## D139. Shipping checks a cache against the session, and falls back to the text

*Gap 33, as D138 decided. This changes D70 Q2 ("shipping trusts the cache") for this one
check.*

- **The failure.** A title update that changes a base macro ships a new base cache, but a
  DLC's cache, baked against the old base, kept the old values in a shipping build:
  measured, the base's sword read 12 and the DLC's axe 10, with no warning
  (`dev/background/content-updates.md`, scenario 11).
- **Now.** `fresh()` is split in two.
  - **`begins_alike`** checks what the recorded load began with: the macros and flags
    (`before`), the pack levels, and the files `#require` had read. All of it is in
    memory, so no file is read.
  - **The rest** of `fresh()` re-reads the recorded files.
  - **Development** checks `begins_alike` first, so a session that differs no longer
    re-reads every file to find out.
  - **Shipping** (`read_cached`, and a Config's `cache_dir` reads) runs only
    `begins_alike`, through `detail::read_shipped`:
    - a cache for this session is used;
    - otherwise the text is read;
    - when a cache exists but was baked for another session and there is no text, the read
      is refused (`ErrorCode::Refused`, located at the cache). The message names what to
      check: macros, pack levels, or required files.
  - Shipping still does not re-read the files a cache recorded: an edited text with
    unchanged macros keeps its cache, as before. That is the decided scope (no I/O).
  - A cache baked without a recorded load has nothing to check and is still trusted.
- **Measured.**
  - **The research's driver** (title update, then shipping): with the text shipped, the
    DLC now reads 12. With caches only, the layer is refused with the located error.
  - **Cost** (`read_cached` of a small file in an optimized shipping build, pinned, best
    of 9 × 200 reads, three runs):

    | | before | after |
    |---|---:|---:|
    | no macros | 2.88–2.96 µs | 3.04–3.13 µs |
    | 200 macros | 8.63–8.72 µs | 18.27–18.64 µs |

  - About 50 ns per macro per layer read. That is 0.2 ms for twenty layers over 200
    macros, at load.
  - The local matrix's `ship` configuration sets no build type and is unoptimized (about
    10 times slower here). These numbers come from a Release shipping build made for the
    purpose.
- **Test:**
  - a DLC cache baked with `DMG` 10 is read for a session where `DMG` is 12, and gives 12
    in both builds;
  - in shipping, the same read with the text deleted is refused, and with a matching
    session it uses the cache.

  It fails against the previous code: shipping returned 10.

## D140. A cache is named by the normalized path, and a shipping fallback says why

*Gap 40, as D138 decided.*

- **Names.** A Config named a layer's cache by the hash of its path string as written, so
  `dlc/axe.dta`, `./dlc/axe.dta` and `dlc/../dlc/axe.dta` were three caches. The bake
  machine and the game had to agree byte for byte, and a DLC installed under a
  differently spelled path missed its cache (research scenario 12). The name now hashes
  the normalized path.
  - Every existing cache name changes once. Development rebuilds; shipped caches must be
    re-baked (breaking changes are allowed).
- **Saying why.** When shipping reads the text instead of a cache, it now adds a warning to
  the session, located at the cache:
  - "no usable cache for X: <reason>" when the cache is missing, damaged, or of another
    format;
  - "... was baked for other macros, pack levels, or required files" when D139 set it
    aside.

  A Config forwards session warnings to its warning handler, as it does the one-way rule's
  (D80). When the text cannot be read either, the error carries both reasons.
  - New: `SessionAccess::add_warning`.
  - Development stays silent: it rebuilds caches as part of its work.
- **Tests:**
  - in development, a layer loaded under two spellings of its path writes one cache;
  - in shipping, the missing-cache and stale-cache fallbacks warn, and with no text the
    error names the cache.

  The naming test fails against the previous code: two caches.

## D141. A pack whose required pack is not loaded is warned about

*Gap 41, as D138 decided.*

- **Before.** `(pack expansion (level dlc) (requires season_pass))` was recorded and never
  checked. A DLC whose prerequisite was not installed loaded silently (research
  scenario 7).
- **Now.** A pack's `requires` is checked once its file has been read, so the cookbook's
  pattern of declaring a requirement and then `#require`-ing it on the next line is
  satisfied.
  - **What counts as loaded:** any pack the session has registered, from a layer below or
    earlier in the read.
  - **When one is missing:** a located warning, at the declaration's line, "pack
    'expansion' requires 'season_pass', which is not loaded", goes to `warnings()`. A
    Config forwards it to its warning handler, as it does the one-way rule's (D80). The
    load still succeeds: the rule warns and never fails.
  - **A cache load** replays its packs and then checks the new ones, at line 1, because a
    cache keeps no lines.
  - One helper, `SessionAccess::warn_missing_requires`, serves both paths.
- **Test:**
  - the cookbook pattern gives no warning;
  - an unmet requirement warns at `expansion.dta:2`;
  - both behave the same loaded from a cache.

  It fails against the previous code, which never warned.

## D142. A text-only session for mods, and keyed tables as the content convention

*Gaps 43 and 36, as D138 decided.*

- **`LoadSession::text_only`** (gap 43) refuses every cache.
  - `read_cache` and `read_cache_bytes` return `ErrorCode::Refused` ("... is a cache, and
    this session reads text only").
  - `read_cached` and a Config's `cache_dir` read the text instead, with D140's warning in
    shipping.
  - Copies of the session carry the option: it is one of the public options
    `copy_options` copies (D136).
  - **Why:** a cache is a memory image whose offsets a forged file can point anywhere
    (D71), while the parser takes any input without crashing. A loader of content from
    outside the game's own packages (mods, user files) sets it. The cookbook states the
    rule.
- **Keyed tables** (gap 36): the cookbook's new section on content packages makes keyed
  children the convention for anything packages extend.
  - There is no new syntax or check. Replacing a value list is also the normal, intended
    way a patch changes a value, so a warning could not tell the two apart.
  - `layer_effect` already lists what a layer replaces.
- **The same section** says how a game stacks its packages:
  - optional DLC layers, with fallbacks in code;
  - title updates change values, not the macros below them (research scenario 4);
  - declare `requires` (D141);
  - mods are text.

  The cache section now describes D139's shipping check and D140's names.
- **Test** (text-only): `read_cache` and `read_cache_bytes` are refused, `read_cached`
  reads the text, and a copy of the session refuses too.

## D143. A layer removes a key with `(key #remove)`

*Gap 35, as D138 decided. The owner chose the spelling: the marker sits in the key's own
array, as Module Manager's `!NODE` sits on the node.*

- **Syntax.**
  - `#remove` is a directive the parser accepts only as a tagged array's one value:
    `(enemies (exploit_boss #remove))`.
  - It is stored as the reserved symbol `#remove`.
  - `(a #remove 1)`, `(a 1 #remove)`, `(#remove)`, a bare `#remove` and `{f #remove}`
    are located errors: "#remove is written as a key's only value: (key #remove)".
  - The printer writes the symbol back as `#remove`, so text round-trips.
- **Semantics** (Config layers only):
  - In an Overwrite merge, an incoming `(key #remove)` removes every child tagged `key`
    from the layers below, with its spans. Removals are applied after the rest of the
    level is merged, so the tag index stays valid.
  - A key the layers below lack removes nothing.
  - FillMissing (`#merge`) ignores the marker.
  - What a layer adds to the stack never carries a marker. An appended subtree holding one
    is copied without it (`without_removals`), so a bottom layer's markers, or one inside
    a key new to the stack, never reach the merged tree.
  - Removing the layer brings the key back. A layer above may set it again.
- **`Array::has_removal()`**: a flag set by `finish()` when an array is a marker or holds
  one at any depth. The merge copies a subtree only when the flag says it must, so data
  without markers pays one flag test per appended subtree. The flag is in the array
  header, so caches keep it (no format change).
- **Tools.**
  - `layer_effect` and `diff` list a removal with the value `#remove`, and `below` shows
    what it removed.
  - `promote` refuses a removal leaf with a located message, because writing
    `(key #remove)` beside a key's children would not parse.
  - Write-back never produces a marker.
- **Cost.**
  - `bench_parse` (4.2 MB corpus, callgrind): 4,131.2M → 4,177.3M instructions (+1.1%),
    29.5M of them in `finish()`.
    - A first version tested each child separately and cost +4.0%.
    - The flag is now ORed in the pass that already reads each child's height.
  - Pinned wall-clock runs read 101–126 MB/s for both builds, so noise exceeds the
    difference.
  - The merge benchmark is unchanged in kind: one flag test per appended subtree.
- **Tests:**
  - research scenario 5: the debug menu flag, the exploit boss and a whole `cheats`
    section are removed;
  - `layer_effect` shows a removal and what it removed;
  - a DLC above sets a removed key again, and removing the layer restores the keys;
  - a bottom-layer marker and a marker inside a new key are absent from the tree;
  - a cache round trip keeps the flag and still removes;
  - the bad forms are errors, the marker prints back, and `#merge` ignores it.
- **Fuzzer finding (PR #17).** A quoted `'#remove'` anywhere else is an ordinary symbol, but
  `to_dta` printed it bare, which the reparse refused. Now `#remove` prints bare only as
  a parenthesized array's second of two nodes after a symbol, where the parser takes it,
  and quoted elsewhere. The input is `tests/fuzz_regressions/quoted_remove_symbol.bin`.

## D144. A layer can be placed below or above another

*Gap 37, as D138 decided. The owner chose `LayerOptions` over a separate `insert_layer`.*

- **Before.** `push_layer` always put the layer on top. A title update that fixes a DLC's
  value lost when the DLC was installed, and so pushed, after it (research scenario 3:
  140 instead of 141).
- **Now.** `LayerOptions::below` and `::above` name the layer to place the new one under or
  over.
  - `"scratch"` as the anchor means the top, as does no anchor.
  - An unknown anchor is `NotFound`. Both at once is refused.
  - The views are read at the push and cleared before the options are kept (`placement`),
    so a layer never holds the caller's strings.
- **Re-reading.**
  - A file layer placed under others is read with the session below its place. The layers
    above are then read again, all or nothing, as `remove_layer` does, because they read
    with the macros below them.
  - An overlay adds no macros, so placing one re-reads nothing.
- **Test:**
  - scenario 3: a DLC pushed `below` the title update now reads 141, and keeps its other
    keys;
  - a layer placed `above` the DLC defines `HARD`, and the layer above it is re-read and
    takes its `#ifdef HARD` branch;
  - overlays placed below the base and below `scratch`;
  - the two refusals leave the stack unchanged.

## D145. A softer pack may narrow a harder pack's shape, not loosen it

*Gap 38, as D138 decided. This extends D80's one-way rule to D84's shapes.*

- **Before.** Shapes merge as data (D84), so any later layer could retune them. A mod that
  redeclared `(speed int (range 1 1000))` silenced the base game's range check (research
  scenario 9).
- **Now.**
  - **`Set::add(tree, report, packs)`.** With a session's packs, a field declared again by
    a softer pack (a higher rank) than the one that declared it is held to the harder
    declaration.
    - It may narrow a range or add metadata.
    - Widening or dropping a range, changing the type (or list element, or shape), or
      making a required field optional is a located warning. The harder declaration stays
      in force.
    - A harder pack above a softer one may change anything.
    - Files that declare no pack are unconstrained, as the one-way rule leaves them.
  - **`shape::validator(cfg)`** replaces `validator()`. It reads the shapes of each
    enabled layer bottom to top, with the Config's read session (D110), and checks the
    merged root against them. Before, it read the merged root's shapes, where a
    declaration a later layer overrode was already gone.
  - All callers (stacks, the ImGui harness, tests, `language.md`) pass the Config.
- **`Config::layer_root(name)`**: a layer's own tree, before any merge; null for an
  unknown layer and for scratch. The validator reads it.
- **Test** (packs `game` over `mod`):
  - a mod widening `speed`'s range and making `name` optional is refused twice, and its
    `speed 900` is still out of the base's range;
  - a mod narrowing the range takes effect;
  - a type change is refused;
  - unpacked files behave as before.

## D146. Script a removed layer defined goes with it

*Gap 39, as D138 decided.*

- **Before.** A host ran each layer's `#autorun` commands on its Context after a stack
  change. A function or handler a removed layer had defined stayed defined: an uninstalled
  DLC's `{dlc_bonus}` still ran (research scenario 8), and a native it had
  `{override}`-n stayed replaced.
- **Now: `Context::run_autorun(commands)`.**
  - It first restores everything the previous `run_autorun`'s commands defined or
    replaced, newest first, then runs `commands` in order.
  - So after a stack change, the script state autorun made is exactly what the current
    stack's autorun makes:
    - a removed layer's functions and handlers are gone;
    - a native they overrode comes back;
    - a lower layer's definition of the same name, re-run, is the one in force.
  - A failing command does not stop the rest; the failures are returned as a list (empty
    on success).
- **How.**
  - While `run_autorun` runs, `{func}`/`{override}`, `{class}` and `{extend}` record the
    name's state before its first change: a function's overloads, tier, status and doc,
    or a class's handlers (`note_function`, `note_class`).
  - The next run restores them through `retire()`, so a body still running finishes on its
    own definition (D117's rule for redefinitions).
  - Definitions made outside `run_autorun`, such as a console's `eval` or the host's
    natives, are not recorded. If autorun later redefines one of those names, the next run
    restores it to what it was before autorun touched it.
  - Variables and objects that autorun commands made are not undone. The decision covered
    functions and handlers.
- **No Config change.** The stacks' `Live::autorun` now calls `run_autorun`. The cookbook
  and `Config::autorun()`'s comment say to.
- **Test:**
  - base and DLC autorun, then base alone: the DLC's function is gone, the overridden
    native is back, the base's `greet` is back, and the class's handlers are restored,
    with the DLC's added handler gone;
  - a console function is untouched;
  - an empty run undoes everything autorun defined;
  - a failing command is reported while the next still runs.

## D147. Packs declare versions, and a required version is checked

*Gap 42, as D138 decided.*

- **Syntax.**
  - `(version "1.2")` in a pack declaration.
  - A `requires` entry may be `(name "version")`, meaning that pack at that version or
    later: `(requires (base_game "1.2") season_pass)`.
  - A version is dot-separated integers, written as a string, a symbol, or a non-negative
    integer. A float is refused with "write a version as a string", because `1.10` would
    read as `1.1`. Anything else is a located error.
  - Versions compare part by part, numerically, with a missing part counting as 0. So
    `"1.10"` is after `"1.2"`, and `"2"` equals `"2.0"`.
- **The check** is D141's, once the pack's file is read and after a cache replay. A required
  pack that is older, or declares no version, is a located warning ("... requires 'base'
  "1.10" or later, which is "1.2""). The load goes on.
- **`Pack`** gains `version` and `requires_versions`, parallel to `requires_packs` (empty:
  any version).
- **Caches record both.** A versioned requirement is stored as `(name "version")`, and the
  pack entry gains its version. That changes the recorded load's layout, so the cache
  format is now version 5: older caches are refused, and development rebuilds them.
- **Test:**
  - a met requirement gives no warning;
  - `"1.10"` against `"1.2"` warns, part by part;
  - an integer version satisfies `"1.9.3"`;
  - a pack without a version warns;
  - four malformed declarations are errors;
  - a cache keeps the versions and checks them on replay.

## D148. The cache is laid out for delta patches

*Gap 34, as D138 decided: the owner chose a delta-friendly `.datb` over text-only updates or
an immutable base cache.*

- **Before.** A cache was a memory image of absolute offsets: arrays breadth first, then the
  strings, then a symbol table indexed in order of first use. One new array, string or
  symbol moved what followed it, and every offset or index that pointed past it changed.
  An inserted section rewrote all ten 1 MB chunks of a 9.6 MB cache, a 135 KB xdelta
  patch where the text's was 138 bytes (content-updates.md).
- **Now: format version 6, in source order, with nothing absolute in it.**
  - Each array's record (its header, tag run and nodes, as before) is followed by what its
    nodes hold, in order: a string inline, a child array as its own record and what follows
    it. The loader walks the records in the order they were written and points each array
    or string node at the next record, so those nodes hold 0 in the file.
  - A target the tree shares, written before, is the distance back to it from the walk's
    point.
  - A symbol, var or file is the 64-bit hash of its name. The table at the end is sorted
    by home slot, then by hash, so the same names make the same table in any order, and a
    new name moves only its own run of slots. Two names that hash alike refuse the write.
  - A line is the difference from the line of the array before it. Block fields and tag
    runs are 0; the loader sets them, the tag run from each child once it is whole.
  - The loader keeps the arrays it is inside on a stack of 256 frames, which spills to the
    session's allocator only for deeper trees.
- **Patches** (`content-updates.md`'s file, 12,000 sections, every variant baked under one
  path; `xdelta3 -9`, and 1 MB chunks that differ at the same offset):

  | Change | Text | Format 5 | Format 6 | Format 5 chunks | Format 6 chunks |
  |---|---:|---:|---:|---:|---:|
  | one float value | 62 B | 214 B | 159 B | 3/10 | 3/10 |
  | one longer string | 161 B | 2,841 B | 286 B | 6/10 | 7/10 |
  | a key the file never had | 107 B | 85,634 B | 323 B | 10/10 | 7/10 |
  | insert a section | 138 B | 134,918 B | 352 B | 10/10 | 10/10 |
  | append a section | 260 B | 128,669 B | 339 B | 10/10 | 10/10 |
  | remove a section | 61 B | 119,407 B | 291 B | 10/10 | 10/10 |

  A chunked store that compares at fixed offsets still sees every chunk after an insert
  move, as it does for the text. A store with content-defined chunks, or a binary diff,
  sends about what the text's patch sends.
- **Size.** 9,994,400 bytes against 9,566,304 (+4.5%: the symbol table's 16-byte slots at
  half load). Compressed it is smaller, since offsets no longer make every payload unique:
  gzip -9 469,444 against 1,047,693, xz -9 264,616 against 453,956.
- **Load: 4–15% slower,** measured against D147's commit on the same machine with the same
  files (release, pinned to one core):
  - this file's cache, best of 3×15 loads: 3.47 against 3.71 ms; 81.5M against 97.3M
    instructions for three loads (cachegrind);
  - `bench_cache`, `read_cache` from a file: 2.00 against 2.10 ms (1.2 MB of text) and
    7.4–7.9 against 8.2–8.6 ms (4.2 MB, whose sections are mostly symbols);
  - the cost is the symbol lookup, a probe from the name's home slot where format 5 indexed
    a table, and the walk's stack. A first version, which visited every node twice, was
    +19% on this file; one pass and a hoisted mask brought it here.
  - `write_cache`: 14.0 against 7.6 ms (1.2 MB), 44.1 against 50.5 ms (4.2 MB).
  - `bench_cache`'s "Config layer, cache_dir, fresh cache" row read 33–38 against 56 ms at
    4.2 MB, with the same instructions (callgrind, +1.7%) and the same simulated cache
    misses. That is glibc's dynamic mmap threshold: the 22 MB buffers sit near it, and the
    larger cache tips them to fresh mappings and page faults. With
    `MALLOC_MMAP_THRESHOLD_` pinned high the row reads 21.0–24.1 against 20.5–21.4 ms, and
    pinned low 69 against 74 ms.
- **An update bakes under the base's path.** Every array names its file, by hash now, so a
  cache baked from `d_value.dta` differs from one baked from `d_base.dta` in every array.
  The cookbook says so.
- **Test:**
  - an edit's locality: inserting a section into 60 leaves 677 of 678 32-byte blocks of
    the old cache somewhere in the new one (format 5: 289 of 672);
  - names that differ only by length and a first byte (`''`, `'\x01'`) hash apart. The
    fuzzer found them colliding: the checksum started from the length, which a first byte
    equal to it cancelled. It now folds the length in after the bytes, which also changes
    `content_hash` (format 6 rebuilds every cache anyway);
  - a 300-deep tree round-trips, past the loader's inline stack, checked without recursion
    (a 600-deep check overflowed MSVC's debug stack in its recursive helpers);
  - the existing tests of sharing, damage, and every corpus file loading as its text did.

## D149. A Config reads a layer's cache straight into its arena

*The owner's pick after D138: D148's benchmark showed a cached Config layer at 33–55 ms
where `read_cache` took 8 ms on the same 22 MB cache.*

- **Before.** A Config wraps its session's reader so that each layer records the text files
  it read, for reload. The cache file went through that wrapper too, which made the cache
  look like a reader's file rather than the disk's, so it was copied four times:
  - `read_disk_file`'s 64 KB buffer;
  - the Config's `scan` buffer;
  - a fresh string in `open()`;
  - the fresh arena.
  The two fresh 22 MB buffers also took page faults, more so when glibc's mmap threshold
  tipped them to new mappings (D148).
- **Now.** When the Config's own reader is the disk's, `read_through_cache` passes
  `from_disk`, and `open()` reads the cache file with `fopen` straight into its arena, as
  `read_cache` does.
  - The text files the freshness check reads still go through the wrapper, which records
    them. The wrapper never recorded the cache file.
  - A Config with a custom reader, such as a pack file system's, still reads the cache
    through that reader.
  - A cache read from disk is no longer timed as a read, so a reload outside edit mode
    counts it in `ReloadRecord::parse_ns`.
- **Benchmarks** (`bench_cache`, release, pinned to one core, three runs each, against
  `main` on the same machine):

  | Row | 1.2 MB text | 4.2 MB text |
  |---|---:|---:|
  | Config layer, fresh cache, before | 4.2–4.6 ms | 47–55 ms |
  | Config layer, fresh cache, now | 2.6–3.1 ms | 11.6–14.4 ms |
  | the same with `MALLOC_MMAP_THRESHOLD_` pinned high, before | 4.2–4.6 ms | 20.8–24.9 ms |
  | the same, now | 2.6–3.1 ms | 11.1–14.3 ms |
  | `read_cache` alone (unchanged) | 2.0–2.2 ms | 8.4–9.1 ms |

  What remains above `read_cache` is the freshness check, which reads and hashes the
  4.2 MB text, plus the layer's merge.
- **Test:** a Config with a logging reader asks it for the cache once and the text once
  (freshness). A Config with the disk's reader loads the same values from the cache.

## D150. Releases publish the core library to github.com/zach/baker

*The owner asked for a process that publishes the core library alone to a separate
repository, tagged with semantic versions from 0.1.0, by an automatic process that runs only
when asked. Their choices: library, tests, examples and user docs ship; citations are
stripped on publish; one commit per release; a hand-run GitHub Actions workflow; the
LICENSE names Zach Baker; notes come from CHANGELOG.md; the public repository runs CI of its
own. The process is `dev/releasing.md`.*

- **What ships** is `tools/release/manifest.txt`. It ships the library, `tests/`, `fuzz/`,
  `examples/`, `bench/`, the tools the build and tests run, and the four user docs, with
  `tools/release/public/` laid over it (a trimmed CI workflow, a `.gitignore`). It leaves
  out `stacks/`, `.claude/`, `CLAUDE.md`, `dev/spec.md`, this file, `dev/background/`,
  `dev/allocation-review.md`, `tools/release/` and the dev-only tools.
- **Citations.** `publish.py` strips parentheticals made only of citations (decision
  numbers, spec sections, background notes, gap numbers) and keeps the words of mixed ones.
  A citation that began a line hands its punctuation to the line before, and a comment that
  was only a citation goes. A published line that still names what the release leaves out
  fails the release.
  - The first dry run left 34 such lines, citations in running text or wrapped across lines.
    They were reworded here. `write.hpp` now lists the values the printer cannot spell
    instead of pointing at D13.
  - 417 lines change on the way out. The stripped tree passes clang-format and builds, and
    all 17 tests pass on it alone.
- **Guard.** `publish.py check` (CI's "release dry run" job, `tools/local_matrix.sh`)
  prepares the release at the declared version on every change. A sentence the release
  could not publish therefore fails where it is written, not on release day.
- **History.** Each release is one commit on zach/baker's `main`, the child of the release
  before, titled "Baker X.Y.Z" with the changelog entry as its message. It is tagged
  `vX.Y.Z` and published as a GitHub release. The first release follows zach/baker's
  initial commit, whose LICENSE already names Zach Baker; baker-dev's LICENSE now matches it.
- **Versions.** `publish.py bump` sets the version in CMake, `version.hpp`, `version.cpp`
  and the README's `FetchContent` tag, and opens a changelog entry. A release refuses a tree
  whose version files disagree, a missing or TODO entry, or a version not above the newest
  tag. The tree now declares 0.1.0, ready for the first release.
- **The workflow** (`.github/workflows/release.yml`, `workflow_dispatch` only, from `main`):
  - `prepare` builds and checks the tree;
  - `verify` builds and tests it alone with GCC, Clang with sanitizers, shipping and MSVC,
    and checks its format;
  - `publish` commits, tags, pushes and creates the release, only with *publish* checked,
    through the `release` environment the owner can gate with an approval. It needs the
    `BAKER_PUBLISH_TOKEN` secret.
- **Agents** start, prepare or publish a release only at the owner's explicit request
  (CLAUDE.md).

## D151. Integrations stay outside Baker, and their needs cross as requirements

*The owner plans to try Baker with copyleft engines, source-available systems such as
SpacetimeDB, and commercial offerings, and asked how to do that without encumbering
Baker's license or making it look derived from what motivates its features.*

- **Policy: `INTEGRATIONS.md`,** at the top level. Its rules:
  - dependencies point one way, from an integration to Baker;
  - each integration lives in its own repository, licensed as its target requires, and
    private when it touches confidential material;
  - needs cross into Baker as requirement notes in Baker's terms, never as code;
  - facts may cross, expression may not;
  - confidential information stays in the integration's repository;
  - Baker's record states its own general reasons;
  - provenance is kept: a Developer Certificate of Origin sign-off for contributions from
    anyone but the owner, and integration work in separate sessions from Baker work;
  - names are used descriptively.
- **Mechanisms: CLAUDE.md's "Integrations" section.**
  - No dependency on a restrictive target here, even an optional one.
  - Features are built from the requirement note alone, tested against a mock host.
  - A Baker session that drifts into an integration's code or confidential material stops
    and asks for the work to move.
  - Records are written in Baker's terms.
  - When in doubt, nothing crosses.
- **Template: `tools/integration-template/`,** to start an integration repository. It holds
  a README with a license notice and no-endorsement line, a CLAUDE.md for agents working
  there, and `requirements/0000-template.md`, the requirement note. A note states the need,
  the general use case, the constraints, the workaround today, and an affirmation that it
  holds nothing from the target.
- **Not published.** `INTEGRATIONS.md` and the template describe how baker-dev is worked on,
  so the release manifest leaves them out, as it does CLAUDE.md. The owner chose no
  forbidden-terms check in the release.

## D152. Documentation is split into docs/ for users and dev/ for development

*The owner asked for user and development documentation in separate folders, so that a
release ships the user side whole. They chose `dev/` at the top level for the development
side.*

- **`docs/`** holds the user documentation: `language.md`, `cookbook.md`,
  `script-reference.md` and `check-json.md`. The release manifest ships `docs/**`, so a new
  guide ships without a manifest change.
- **`dev/`** holds the development documentation, which never ships:
  - `decisions.md` (this file);
  - `spec.md`;
  - `background/`;
  - `allocation-review.md`;
  - `releasing.md`;
  - any milestone plan.
- Every reference to a moved document was rewritten, in code comments, CLAUDE.md, the
  skills, the stacks' docs and the decision records themselves. The publisher's citation
  patterns now read `dev/`. The release check refuses a published line that names `dev/`,
  except a path such as `/dev/null`.

## D153. Integrations to pursue: shared Baker features first, then the agent bridge

*The owner asked for a competitive analysis of Baker against the data, tuning and scripting
systems of larger engines and services, a choice of the most valuable integrations, and
critiqued plans for them. The analysis is `dev/background/integration-landscape.md`; the
plans are `dev/integration-plans.md`.*

- **Finding.** No engine, service or library surveyed ships one-line write-back of a value
  tuned live, per-value provenance, or one stack for local and live layers. The pairings
  that would show this best need the same few Baker features, so most of the work is
  Baker's own and the integrations stay thin.
- **Chosen integrations:** an agent bridge (MCP), editor tooling (tree-sitter and an LSP
  driven by `check`), Godot through GDExtension, and published live-ops overlays. Unreal,
  a spreadsheet round-trip and a Tracy adapter were considered and deferred.
- **The owner's choices:**
  - Start with the shared features E1 (`explain`), E2 (a JSON reply form and the Config's
    tools as script functions) and E3 (a machine-readable reference), then plan A, the
    agent bridge, with its evaluation on the stacks.
  - Each integration lives in its own repository from `tools/integration-template/`,
    including the MIT-only ones (`INTEGRATIONS.md`, rule 2).
  - E2's reply form and `bind_config_tools` ship in the published library.

## D154. `Config::explain`: a path's whole layer chain

*E1 of the tool surface (`dev/tool-surface-plan.md` §1), the first step D153 chose. Tools,
editors and agents needed every contributor to one value; `source()` names the winner
only, and `layer_effect` works one layer at a time.*

- **API.** `Config::explain(path)` returns `List<ExplainEntry>`, bottom to top. Each entry
  has `layer`, `value` (packed as `layer_effect` packs it), `source` and `standing`:
  - `Wins`: the value `get` returns;
  - `Shadowed`: overridden or removed from above;
  - `Removes`: `(key #remove)` at the path or an ancestor;
  - `Disabled`: a disabled layer, or disabled scratch, that has the path.
- **Rules.**
  - The walk goes top down: scratch, the layers, then the code default. The first enabled
    contributor with a value wins, or with a marker removes. Every enabled one below it
    is shadowed.
  - A layer's value is what `supplier()` already decides, so the winner is the layer
    `source()` names. A test checks this, and the value against `get`, for every leaf of a
    three-layer stack with a removal.
  - An ancestor's marker is found by resolving the path's prefixes in each layer. The
    marker's own line is its source.
  - A tweak at the path adds a first entry, layer `code`, holding its default. It wins only
    when nothing above sets the path. This needed `TweakBase::default_values(ValueList&)`,
    a second virtual beside `values`; the tweak read is unchanged.
  - A path that names arrays of arrays is refused ("holds arrays, not values: explain a
    leaf"). A path nothing sets fails as `get` fails.
  - The marker symbol prints quoted when it stands alone, `'#remove'`, as D143 has it.
- **Cost.** Tool path. It allocates its result, and the work is one resolve per prefix per
  layer. It is available in shipping builds, as `source()` is.
- **Tests:**
  - `tests/data/test_explain.cpp`: overrides, an index path, removals of a key and of an
    ancestor, a key set again above a removal, a tweak's default alone and shadowed,
    refusals, a disabled layer and scratch, and the agreement with `source()` and `get`.
  - `baker_alloc_tests`: a Config answering `explain` through its own allocator.
- **Core:** 11,989 → 12,085 lines.

## D155. `eval_request` and the reply form `baker.reply/1`

*E2a of the tool surface (`dev/tool-surface-plan.md` §3). Every tool that drove the
console parsed `=> value` text and prose errors. The owner chose JSON for request-form
lines with the human reply kept for bare ones, and keyed arrays projected to objects.*

- **API.** `eval_request(ctx, line, sink)` in `baker::script` writes one JSON line and never
  fails.
  - A request is `(ID {command})`, with ID an int, symbol or string, echoed back.
  - Any other line is evaluated whole, as `eval(text)` does, with a `null` id.
  - A line that does not parse gets an error reply.
  - The tree is built from the Context's allocator, as `eval(text)` builds it.
- **The reply** (`docs/reply-json.md`):
  - On success: `text`, the printed result and the authority, and `value`, a JSON
    projection.
  - On failure: `error` with `code`, `file`, `line`, `path`, `message`, and the script
    `stack`.
  - Baker reads no JSON. Requests are Baker text.
- **The projection.**
  - A keyed array, whose children after an optional leading symbol are all arrays headed by
    distinct symbols, is an object. Its leading symbol is dropped, and keyed children nest.
  - Floats use the printer's shortest form, which is valid JSON; a non-finite float is
    `null`.
  - The first version projected a keyed child's rest as an array, so
    `(cfg (box (width 2)))` read as `{"box":[["width",2]]}`. The tests caught it: a child
    that is itself keyed is now an object.
- **The console.** `examples/remote-console` answers a line beginning with `(` through
  `eval_request`, and any other line as before. Replies still end with an empty line, so
  clients frame both alike. The stacks build this console, so their games answer requests
  too.
  - `stacks/tools/console.py` sends `(N {command})`, parses the JSON, prints `=> text` or
    the error with its stack, and keeps its exit codes (1 for an error reply, 2 when nothing
    listens).
  - `--json` prints the raw replies.
- **Not fuzzed.** The fuzz harness stays data-only. The parse is fuzzed already, and
  fuzzing evaluation would run arbitrary script.
- **Tests:**
  - `tests/script/test_request.cpp`: ids of each kind, bare lines, every projection rule
    (nesting, null rests, repeated tags, ordered lists), an error two script frames deep,
    a parse error, and escaping onto one line.
  - The remote-console example's CTest checks a JSON reply. A deliberately wrong id was
    seen to fail it.
- **Core:** 12,085 → 12,229 lines, the JSON move included.
- **Also in this step:** the JSON writing helpers moved from `reference.cpp` to `data/text`,
  shared by all three libraries. `check`'s bytes are unchanged.

## D156. `bind_config_tools`: the Config's tooling as script functions

*E2b of the tool surface (`dev/tool-surface-plan.md` §4). An agent or an editor talking
to a running game needs the Config's answers (provenance, layers, live edits), not only
`eval`. The owner chose read-only tools in shipping builds.*

- **API.** `bind_config_tools(ctx, cfg, {.allow_write})` in `baker::script` returns
  `Expected<void>`. Every function is documented, so `reference` lists it and `check`
  checks calls to it. Results are data, which `eval_request` projects to JSON.
  - **Every build:** `config.get`, `config.source`, `config.explain`, `config.layers`,
    `config.effect`, `config.diff`.
  - **Development builds:** `config.set`, `config.toggle`, `config.reload` (trigger
    `console`), `config.unsaved`, `config.journal [since_ns]`, `config.loop`, and
    `config.snapshot` and `config.restore` by name.
  - **Only with `allow_write`:** `config.write_back` and `config.promote`, which return the
    lines written and any refusals.
- **Arguments.** Paths and layer names may be written as symbols or strings
  (`box.width`, `"phase.2"`), through a file-local `Convert<Name>`. Binding them as
  `string_view` refused symbols.
- **Values** are packed as `explain` packs them: one value bare, several as an array.
  - The journal's `before` and `after` first came out as one-element lists; they are now
    packed the same way and left out when there is no value.
  - `config.get` is the entry `explain` says wins, so it agrees with `get` (D154's test).
    The code default does not count: an unset path is a located error.
- **State.** Named snapshots live in one block (`Rc<Tools>`), allocated from the
  Context's allocator and shared by the bound functions. It is freed with the last of
  them. `baker_alloc_tests` binds the tools, evaluates `explain` and a snapshot, answers a
  request, and checks that nothing reaches `operator new` and everything is freed.
- **The stacks** bind the tools in `Live::open`, without `allow_write`, so the console and
  `stacks/tools/console.py` can ask a running game `{config.explain p}`. Writing stays with
  the person at the panel.
- **Core:** 12,229 → 12,556 lines.
- **Shipped as one step with D155** (E2). The matrix for E2a alone ran while E2b was
  being written, so its results counted for neither, and the two went in together.
- **A test race fixed.** `TempDir` named directories with an unseeded `rand()`, so every
  test process chose the same names. Under `ctest -j`, the first script test to use one
  could delete a data test's files: the matrix's release run failed once and passed on a
  rerun. Names now carry a per-process tag from `std::random_device`.
- **Tests.** `tests/script/test_config_tools.cpp`:
  - every reading tool, against a two-layer Config on disk;
  - the JSON reply for `explain`;
  - `check` finding a wrong-arity call, and the reference listing the tools;
  - the live tools: set, unsaved, the journal, toggles, snapshot and restore, reload, loop;
  - the writers absent by default, and writing a one-line change with `allow_write`;
  - a shipping build's reading tools only.

## D157. The vocabulary as JSON: `baker.reference/1`, `baker.tweaks/1`, `baker.shapes/1`

*E3 of the tool surface (`dev/tool-surface-plan.md` §6), and the last step of the three
D153 started with. Agents, editors and engine docks need the functions, tweaks and shapes
as data. The reference was Markdown only.*

- **Three documents, three libraries** (`docs/reference-json.md`). Each is sorted, so a
  program gives the same bytes every run, and each is versioned like `baker.check/1`.
  - `write_reference_json(ctx, sink, packs)` in `baker::script` lists:
    - functions: tier, status, replacement, doc, a script function's `file` and `line`
      (already recorded on `Function`), and overloads with each parameter's type, kind
      and rawness, the result type, `min_args`, and `max_args` (`null` for a rest
      parameter);
    - types: bases, methods, properties, and script handlers;
    - script-only classes;
    - globals;
    - packs and origins, as `check` writes them.
  - `write_json(registry, sink)` in `baker::data`, next to the tweaks:
    - path, type, default, current value, bound;
    - range, step, desc, kind, group, dev_only;
    - source while bound.
  - `shape::write_json(set, sink)` in `baker::shape`: each shape's `at`, keyed, place, and
    fields.
- **A deviation from the note.** The note had `Tweak<T>` record a type name at
  construction. `TweakBase` already knows it through its `ConvertInfo`, so the new public
  `type_name()` reads that, with no new storage. The names are Baker's own (`float32`,
  `int32`), as errors print them.
- **Not in it.** JSON Schema output is left to a tool, which can derive it from
  `baker.shapes/1`. Macro definition sites need a load-path change and wait for plan B's
  go-to-definition.
- **The stacks.** `<game> reference --json` prints the three documents in one object.
  Duel's lists 57 functions, the `config.*` tools among them, 8 tweaks and 4 shapes. The
  stacks' agent docs and skill mention it.
- **Checked.**
  - `tests/script/test_reference_json.cpp`:
    - byte-stable output;
    - a native overload with an optional parameter;
    - a script function's file and line;
    - a rest parameter;
    - a type's method, property and handler;
    - a script-only class;
    - a global;
    - packs and origins.
  - `tests/data/test_tweaks_json.cpp`: the whole document, compared exactly, with a bound
    and an unbound tweak.
  - `tests/shape/test_shape.cpp`: a keyed shape's fields.
  - Python parsed all three documents as JSON.
- **Core:** 12,556 → 12,829 lines. `baker::shape`: 466 → 513 of its 600.

## D158. The check gate: report 1.1, a Config's `#autorun` script, and the stacks' `check`

*Requirement notes 0004 and 0005 from the agent-bridge integration (`baker-mcp`), which
the owner brought here and chose to take up. The design is `dev/check-gate-plan.md`.
`check` is the gate an edit must pass in the stacks' skill, in hooks and in CI. It passed
data that failed to load, missed calls inside script that `#autorun` defines, and kept
shape errors and parse errors out of its JSON.*

- **The cause of the missed calls.** `check` already finds an undefined call inside a
  `{func}` body in the trees it is given. The commands `#autorun` runs are not in the
  merged tree (D107), so checking `cfg.root()` never saw them.
- **Baker.**
  - **The Config form.** `check(cfg, ctx, report, options, extra)` checks the merged tree
    and a tree built from `cfg.autorun()`, each node keeping its file and line. It uses the
    read session as `packs` unless the options give one, and checks any `extra` trees
    alongside.
  - **New fields.** `CheckReport` gains `load`, `shape_errors` and `shape_warnings`, which
    the host fills: `baker::script` does not depend on `baker::shape`, and the load is the
    host's. It also gains `unresolved_fails`, from the new `CheckOptions::unresolved_fails`.
  - **`ok()`.** It now means no errors, no load problems, no shape errors, and, under the
    rule, no unresolved demand. Every caller in the repository still holds: the meaning
    widens only when a host fills the new lists.
  - **The JSON is `baker.check/1.1`.**
    - `passed` comes after `schema`, so a reader need not re-derive the rule.
    - `load`, `shape_errors` and `shape_warnings` take the problem form, after `findings`.
    - The pinned example changed only by these additions.
    - The text form prints them as `load:`, `shape:` and `shape warning:` lines.
- **The stacks.**
  - **`Live`** keeps the located errors of its last load: a layer that did not read, and
    each failed `#autorun` command.
  - **`check`:**
    - It uses the Config form, with scenarios and metrics as `extra`.
    - It fills `load` from `Live` and the shape lists from `shape::check`, and sets
      `unresolved_fails`.
    - Its exit status is `report.ok() ? 0 : 1`.
    - Data that does not load still prints the JSON, with `passed: false` and the error
      in `load`. The exit status stays 2, "could not load", as the note's constraint asks.
    - Standard error keeps its text for people.
  - **Validator warnings stay out of `load`.** They are the shape and call checks again,
    and are reported in their own lists.
- **Tests.**
  - `tests/script/test_check.cpp`:
    - the Config form finds demand inside an `#autorun` function that the merged tree
      alone misses;
    - the demand rule;
    - each new list's effect on `ok()` and its place in the JSON.
  - `stacks/tools/test_commands.py`, run by CTest for Duel where Python is found, on a
    copy of the data. It covers clean data, a failing `#autorun` command, a typo inside an
    `#autorun` function, a value out of range, and a parse error (exit 2 with JSON).
  - Every stack's own data still passes `check`.
- **Core:** 12,829 → 12,867 lines.

## D159. The stacks' commands for tools: `eval --json`, `sim` as JSON with `compare`, `promote --patch`

*Requirement notes 0001, 0002 and 0003 from the agent-bridge integration, which the owner
chose to take up (`dev/check-gate-plan.md` §3–5). They are the stacks' commands, not
Baker's, so Baker's code is unchanged.*

- **`eval --json`** (0001).
  - Each command is answered with one `baker.reply/1` line through `eval_request`, as a
    running game's console answers, with ids counting from 1.
  - Exit 0 when every command ran, 1 when a reply carries an error, 2 when the data does
    not load.
  - A tool now reads a headless game and a live one the same way.
- **`sim` as JSON, and `compare`** (0002).
  - **`stacks.sim/1`.** `sim --out DIR` also writes `results.json`, and `--json` prints it
    instead of the text. It holds:
    - each run (scenario, seed, steps, passed, errors, objectives), sorted by scenario,
      then seed, with objectives by name;
    - a tally per objective (passed, failed, min, median, max);
    - `status`.

    Numbers are printed as the `.dta` results print them, and a value that is not finite is
    `null` with its reason. Two runs give the same bytes.
  - **`compare BEFORE AFTER`.** It reads the `run-*.dta` files two `--out` directories
    hold, so the stacks need no JSON parser and no rerun. It prints `stacks.compare/1`.
    Per objective:
    - `moved`: `broke` (passed on a scenario and seed, now fails), `fixed`, `same`, `new` or
      `gone`;
    - each side's tally and the median's change;
    - the runs whose pass changed.

    Exit 1 when anything broke.
  - **The agent skill now uses them.** The loop saves the baseline with `--out
    runs/before` and the change with `--out runs/after`, over every scenario, then
    compares. In the bridge's evaluation, every failed run, in both arms, was a fix that
    broke another objective on one seed. `/stacks/*/runs/` is ignored.
- **`promote --patch FILE`** (0003).
  - **The promotion is real; only the write is captured.** The Config's writer captures
    each file instead of writing it (D132's hook), so nothing on disk is written, created
    or locked.
  - **The patch.** Each captured file becomes a unified diff against the file on disk,
    with paths relative to the data directory, for `patch -p1` or `git apply`.
    - Promotion is all or nothing, so on a refusal the patch is empty and the exit is 1.
    - The diff is about 90 lines in the stacks' tools: the common head and tail are cut,
      then the middle is compared line by line, then grouped into hunks with three lines
      of context.
    - A last line without a newline is marked as `diff -u` marks it.
  - **Checked.** On Duel's data:
    - the patch applies with `patch -p1` and `git apply`;
    - the patched files equal what `--write` writes, byte for byte, also when the target
      has no final newline;
    - the data directory is unchanged after `--patch`.
- **Tests.** `stacks/tools/test_commands.py` gains each case:
  - one reply line per command, the `explain` chain as JSON, an error reply, and the exit
    codes;
  - `sim --json` equal to `results.json`, byte-stable, and sorted;
  - `compare` moving nothing for identical runs, and naming the broken runs after a
    weaker fighter (exit 1);
  - `--patch` writing nothing, and its application equal to `--write`.
- **CI found a Windows case.** A Windows checkout gives the data CRLF lines, so the patch
  carries `\r` bytes in its content lines. That is still exact: applied with `git apply`,
  it equals `--write` byte for byte, also tried by converting a copy of Duel's data to
  CRLF. But the old GNU `patch` (2.5.9) a Windows runner carries asserted on it. The test
  now applies with `git apply` and falls back to `patch` only where git is absent, and the
  command's message suggests `git apply` first.
- **One commit, not three.** The three changes share `tools.cpp`.

## D160. The stacks' file clock counts from 1970: journal times read negative on Linux

*Reported by the agent-bridge session while testing the live tools: `{config.journal}`
in a stack game returned negative `time_ns`, which broke its `since_ns` filter. It is a
defect in the stacks, not in Baker.*

- **The cause.** `Live` gives the Config `file_clock_now_ns()` as its time source, so that
  edit-to-see runs from a file's save time.
  - That function shifted the file system clock by 2^62 to keep it positive.
  - libstdc++'s file clock counts from 2174, so today is about −4.65×10^18 ns there,
    beyond the shift. The value wrapped to near 2^64.
  - Script integers are 64-bit signed, so `config.journal` (D156) showed it as negative.
  - Windows' file clock counts from 1601, so Windows never showed it.
- **The fix.** `file_clock_ns` converts through `std::chrono::clock_cast` to the system
  clock: nanoseconds since 1970, positive until 2262 on every platform. A time before 1970
  is 0, which the stacks already read as unknown.
- **Checked.**
  - A live cube game's journal entry carries a positive time.
  - `{config.journal T}` keeps the entry made at T, and `{config.journal T+1}` drops it.
  - A new test in `stacks/base/tests/test_watcher.cpp` asserts the clock is below 2^63
    and within a minute of the system clock. It fails on the old clock and passes on the
    new.
- **For Baker.** A host's TimeSource is its own domain (`uint64_t`), but `config.journal`
  and `config.loop` hand times to script as `int64`. A host's clock should stay below
  2^63. The stacks' now does.

## D161. Checking a layer before it takes effect: `LayerPolicy`, `check_layer`

*E5 of `dev/integration-plans.md`, which the owner chose next. The design is
`dev/layer-check-plan.md`. A host taking a layer from outside needs, before it takes
effect, its effect and the verdicts on it, under a policy that bounds what it may change,
with a refusal that changes nothing. The hosts: a live game taking an overlay, an agent's
proposal, a mod loader, CI.*

- **API** (`config.hpp`).
  - **`LayerPolicy`** has `data_only`, `scalars_only`, `existing_only`, `warnings_refuse`,
    and `allow` (a span of path prefixes).
  - **`LayerReport`** has `effect` (as `layer_effect` reports it), `refusals` and
    `warnings`, and `ok()`.
  - **`Config::check_layer(name, tree, policy)`** checks a tree, changing nothing.
  - **`Config::check_layer_text(name, text, policy)`** reads the text first:
    - as text only, never a cache (D142);
    - with a copy of the read session, so the candidate's packs' `requires` and versions
      (D141, D147) are checked against the stack's;
    - only the warnings that read adds are reported.
  - **`LayerOptions::policy`.** With it, `push_layer`, by path or by tree, checks first and
    refuses with the first refusal and a count of the rest, changing nothing. A file
    layer's `#autorun` commands count under `data_only`.
- **The rules.**
  - The candidate's leaves are `layer_effect`'s.
  - `data_only` refuses commands, properties and variables anywhere, and `#autorun`.
  - `scalars_only` refuses a value that is not a number, symbol or string, and a removal.
  - `existing_only` refuses a path the stack does not set, and a removal.
  - `allow` refuses a path outside every prefix, compared segment by segment.
  - The validators run over the stack merged with the candidate on top; what they report
    is a warning.
  - `warnings_refuse` makes each warning a refusal.
  - Each refusal names the path and its reason, located at the array that holds it.
- **The effect is computed with the candidate on top.** A host that places a checked layer
  lower sees what the layer says, not where it would land.
- **Shipping.** It is available, as `source()` and `layer_effect` are: it is what makes
  `live_layers` safe. A test pushes a checked overlay into a frozen Config with
  `live_layers`, refuses a bad one, and rolls back.
- **Cost.**
  - **The case.** A 9.8 KB overlay changing 400 prices in a 2,000-item table (155 KB),
    with every rule on, checks in 820–900 µs (release, three runs, this container). The
    parse is 66–71 µs of that.
  - **The profile.** Callgrind puts 54% in `detail::resolve`: three lookups per changed
    value, each a linear scan of the 2,000-item array. That is Baker's lookup cost on a
    very wide table, which sorted mode exists for, and not this feature's.
  - **The path class.** It runs once per arriving layer, never per frame, so it is left as
    measured.
- **Tests.**
  - `tests/data/test_layer_check.cpp`:
    - the effect, with nothing changed;
    - each rule's refusals, located, and every rule at once passing a balance change;
    - a validator's warning, and `warnings_refuse`;
    - a pack requiring a newer base version, warned about and refused under the flag,
      while one requiring the current version passes;
    - `push_layer` refusing by tree and by file (`#autorun`) with nothing changed, and
      accepting good layers;
    - the frozen live-layers path.
  - `baker_alloc_tests` checks a layer from text through the Config's allocator.
- **Core:** 12,867 → 13,033 lines.

## D162. Published overlays: the convention, and the live-ops example on E5

*Plan D's Baker side (`dev/integration-plans.md` §5), which the owner chose to start. The
publisher, the client helper, and the storage backends are the integration's, in
`baker-liveops`. Baker's part is the convention they share, documented, and an example
that runs it on the policy check (D161).*

- **The convention** (cookbook, "Published overlays").
  - An overlay is a pack: `(pack NAME (level live) (version N) (requires (BASE "V")))`,
    with `(origin ...)` naming the ticket and the publishing tool. The shipped data
    declares itself a pack with a version.
  - The game handles an overlay in order:
    1. **Fetch and verify.** The host fetches it and checks its hash and signature before
       Baker sees any bytes.
    2. **Stage** with `check_layer_text`.
    3. **Apply at a boundary the game chooses**, by path or through a reader, so the pack's
       `requires` and its `origin` are read with it, under the same policy.
    4. **Keep the last good one** and re-stage it at start.
    5. **Roll back** with `remove_layer`.
    6. **Retire it into source** with `promote`, once it sticks.
- **`examples/live-ops`** now runs this. It stages and applies a double-XP overlay; the
  ticket and tool show in `source()`. It refuses three others, each located:
  - a drop rate typed as a percentage, which the shape validator warns about, so under
    `warnings_refuse` it is refused;
  - an overlay made for game data `"1.1"`;
  - one that runs script.

  Then it rolls back. The shapes judge candidates through `shape::validator` inside the
  check, which replaces the example's own merge-and-check helper.
- **One limit, documented.** A tree read separately and pushed with `push_layer(name,
  tree)` carries no pack or origin: those are read with a file. So the convention pushes
  by path, or through a `LoadSession` reader that serves the fetched text by name.
- **Test.** The example's CTest pins the effect, the provenance line, each refusal and the
  rollback. Changing one expected detail was seen to fail it.

## D163. Received text as a layer: `push_layer_text`, and text that reaches no file

*Requirement note 0001 from the live-ops integration, brought by the owner. The design is
`dev/received-layers-plan.md` §3.1. A host that receives a layer's text (a live overlay,
a mod, a proposed change) had two routes, and each lost something.*
- *By path through a reader that serves the text: a Config with `cache_dir` read it through
  the cache, writing one in development and looking for one in shipping, against D142.*
- *As a tree read separately: no pack, no origin, and a policy that never saw the read's
  warnings.*

- **`Config::push_layer_text(name, text, options)`.**
  - **The read.** It reads the text as a file layer is read: with a copy of the session
    below, so its pack, `origin` and `#autorun` are recorded, and `read_session()` and
    `source()` show them.
  - **Text only.** It never uses a cache, written or read, whatever `cache_dir` and the
    build say.
  - **The name.** `name` is the layer's name and the file its sources name.
  - **Re-reads.** The Config keeps the text, shared by snapshots, and reads it again when
    a layer below is pushed or removed.
  - **What refuses it.** `reload()` never sees it change. Write-back refuses its values:
    "pushed as text and has no file".
- **`LoadSession::refuse_includes`** (the owner's choice). It refuses `#include`,
  `#include_opt`, `#require` and `#merge` with a located error: the text may include no
  files. A layer from outside never reaches the game's disk.
  - `push_layer_text` and `check_layer_text` both set it, so a verdict and a push read
    the same way.
  - A host reading untrusted text with `read_string` can set it too.
- **A fix: a push with a policy now sees its read's warnings.**
  - **The bug.** `read()` handed the read's warnings to the warning handler and cleared
    them. Only then did `push_layer` by path ask for them, so it saw none, and a pack's
    unmet `requires` never refused under `warnings_refuse`.
  - **The fix.** `read()` now collects them for the check. A test covers both the path
    and the text push.
- **The live-ops example and the cookbook** now push received text with `push_layer_text`.
  The reader route, and its limit, are gone from "Published overlays".
- **Cost** (shipping, release, this container). The new `bench/bench_layers.cpp` measures
  a 9.8 KB overlay changing 400 prices of a 2,000-item table (122 KB):

  | Row | Time |
  |---|---|
  | `read_string` | 58–65 µs |
  | `push_layer` (tree), no policy | 80 µs |
  | `push_layer_text`, no policy | 145 µs: the read plus the push, as the note asked |
  | `check_layer_text` | 808–824 µs |
  | `push_layer_text` with the policy | 722–898 µs |

  With `shape::validator`, the pushes without a policy are 186 and 251 µs.
- **Tests** (`tests/data/test_received_layers.cpp`):
  - the pack and origin are recorded, and gone after removal;
  - nothing is written to `cache_dir`, and a cache planted under the same name is not read;
  - each include directive is refused, located, by the push and the check;
  - a re-read after a layer is placed below it, and after that layer is removed;
  - write-back refuses;
  - the read's warnings refuse under the policy, by text and by path;
  - the frozen live-layers path.

## D164. A cheaper layer check: an index of each wide array, and refusal paths made only on refusal

*Step 2 of `dev/received-layers-plan.md`; the owner chose to do it in this work. D161's
profile put most of a check in linear lookups of a wide table, and a host whose report no
longer holds (D165) pays for a check at the boundary.*

- **The index.** The check moved to `src/data/config_check.cpp`. A `Finder` there resolves
  paths through an open-addressed index of each array of 32 or more children it searches,
  built once per check. It finds what `Array::try_find` finds: the first child array with
  the tag.
  - The check uses the finder for the stack's value below each changed one, the layer that
    supplies it, and that value's source. These were three linear scans per value before.
  - The lookup path (`detail::resolve`, `try_find`) is unchanged.
- **`data_only`'s walk built a path string for every array it passed.** It now keeps the
  tags on a stack and makes the path only for a refusal.
- **The leaf helpers** (`Leaf`, `values_of`, `leaves_of`) moved from `config_write.cpp`'s
  anonymous namespace to `config_internal`, which the check and write-back share.
- **Cost** (`bench/bench_layers.cpp`, shipping, release, this container: the 9.8 KB overlay
  changing 400 prices of a 2,000-item table, every rule on):

  | Row | Before (D163) | After |
  |---|---|---|
  | `check_layer` (tree) | 723–728 µs | 228–292 µs |
  | `check_layer_text` | 808–824 µs | 302–387 µs |
  | `push_layer` (tree) with the policy | 806–819 µs | 393–445 µs |

  - **Before.** Callgrind put 67% of the instructions in `detail::resolve`.
  - **After.** No function dominates: the leaves' dotted paths (building, parsing and
    interning them), the merge of the candidate over the stack, and `malloc`.
- **Test.** On a 41-child table with a repeated tag, and two layers each wide enough to be
  indexed, every entry's `below` and `below_source` match `get` and `source()`. That
  includes the first of the repeated tags, the supplying layer, and a new key with
  nothing below.

## D165. A report that still holds: `LayerOptions::checked`, and the Config's generation

*Requirement note 0002 from the live-ops integration. The design is
`dev/received-layers-plan.md` §3.2. A host checks a layer when it arrives and applies it
later, at a boundary it chooses, under the same policy. The push checked again, and that
second check was most of the cost of applying, paid where a game has the least time. The
note asked that Baker, not the host, decide whether the first check still holds: `epoch()`
misses changes that alter a verdict (a validator added; a fast-path `set()`).*

- **API** (`config.hpp`).
  - **`LayerOptions::checked`**, with `policy`, is a report from `check_layer` or
    `check_layer_text`. A push by tree or by text takes its verdict if it still holds, and
    checks as without it otherwise. If the report holds and refuses, the push refuses at
    once, changing nothing.
  - **`LayerReport::stamp`** is an opaque `CheckStamp`: the Config, its generation, a hash
    of the name with the text (or with the tree's address), and a hash of the policy.
- **When a report holds.** All of these must be true:
  - the push is by the same Config and on top;
  - the generation is unchanged;
  - the name and text, or the name and tree, are the ones checked;
  - the policy has the same rules and `allow` paths.

  A file pushed by path never matches, because no check reads by path.
- **The generation** is private to the Config.
  - **What changes it:** every merge (in `commit`, so a push, removal, toggle, reload,
    restore or write-back), `add_validator`, `remove_validator`, every `set()` (the fast
    path included: one add on a member it already touches), and each call of the
    non-const `session()`.
  - **Its range.** Each Config starts at a process-wide count shifted 32 bits, so a report
    never holds for another Config built at the same address.
- **What the host is trusted with.** A tree must not change between the check and the
  push. A validator that reads outside the Config (`check_validator` reads a Context) is
  trusted not to have changed its verdict meanwhile. The header says both.
- **The live-ops example** stages, keeps the report, and applies with it. The cookbook's
  "Published overlays" says the same.
- **Cost** (`bench/bench_layers.cpp`, shipping, release; the 9.8 KB overlay):

  | Push | Checks again | Report held | No policy |
  |---|---|---|---|
  | By tree | 378 µs | 87–88 µs | 80 µs |
  | By text | 481–483 µs | 123–154 µs | 143–145 µs |

  With `shape::validator`, a held push is 174–197 µs by tree and 215–271 µs by text.
  Note 0002's goal was under 1 ms of the owning thread's time to apply; a held push meets
  it with room to spare. So, since D164, does one that checks again.
- **Tests** (`tests/data/test_received_layers.cpp`; a validator that counts its calls shows
  whether a push checked):
  - **A held report is used once.** The push and removal it pays for invalidate it.
  - **Each change invalidates:** a push, a removal, a toggle, a validator added and
    removed, `session()`, and the fast-path `set()`.
  - **So does a mismatch:** another text, another policy, placement below, another
    Config's report, another tree with the same content.
  - **A held refusal** refuses with no validator run and the epoch unchanged.

## D166. Validators judge a stack they are given: `ValidatorStack`, and `any_thread`

*Step 4 of `dev/received-layers-plan.md`, for requirement note 0003. The owner's choice:
validators opt in to running on any thread, and the owning thread runs the rest. For
`shape::validator` to opt in, it had to stop reading the Config. It read each enabled
layer's tree and the read session through the Config it held.*

- **API** (`config.hpp`).
  - **`Config::Validator`** takes a `const ValidatorStack&`: the merged root; the enabled
    layers' own trees, bottom to top; and the session. Every caller changed, and there is
    no old form.
  - **`add_validator(validator, ValidatorOptions)`.** `any_thread` says the validator
    reads only its stack and keeps no state. A `LayerChecker` (D167) runs only those.
- **What a validator sees.**
  - **At a merge:** the enabled layers' trees, and `read_session()`.
  - **In a check:** the same with the candidate's tree last, and the candidate's own read
    session when it came as text. So a candidate's shapes count in its check as they will
    after its push.
  - **The layer list** is one vector the Config keeps and refills at each merge. It is
    built only when there are validators.
- **`shape::validator()`** takes no Config and is any-thread:
  `cfg.add_validator(baker::shape::validator(), {.any_thread = true})`.
- **`check_validator(ctx, options)`** reads the stack's root. Without `options.packs` it
  now uses the stack's session, so a host no longer passes `cfg.read_session()` for packs.
  It reads a Context, which is single-threaded, so it is added without `any_thread`.
- **Callers updated:** the stacks, the harness, the live-ops example, the benchmark, the
  tests, the language guide, the cookbook, and acton-review's gap table.
- **Test.** A validator sees the enabled layers bottom to top, skipping a disabled one, and
  the merged root. In a check it sees the candidate last, and the root with the candidate
  merged.

## D167. Checking a layer off the Config's thread: `LayerChecker`

*Requirement note 0003 from the live-ops integration, step 5 of
`dev/received-layers-plan.md`. A host receiving a layer on any frame wants the check
(parse, merge over the stack, validators) off that frame. The owning thread applies the
layer later, at a boundary (D165). The owner chose a checker value, and validators that
opt in to other threads (D166).*

- **API** (`config.hpp`).
  - **`Config::checker(allocator)`**, on the owning thread, returns a `LayerChecker`.
  - **`LayerChecker::check(name, tree, policy)` and `check_text(name, text, policy)`**
    report as `check_layer` and `check_layer_text` do, against the state the checker was
    made from. They read text the same way: text only, no includes.
  - **The checker is copyable;** copies share one refcounted state, and checks may run on
    several threads at once.
- **What the checker keeps.** One block on the host's allocator:
  - the merged root and the enabled layers' trees, shared;
  - the layers' names;
  - a copy of `read_session()`, whose origins name each value's source;
  - copies of the validators added with `any_thread`, and whether others were left out;
  - the Config's address and generation, for the stamp.

  It never touches the Config again, and outlives it: a test destroys the Config and
  checks again.
- **The check is one function of a *basis*** (`config_check.cpp`): the root, the layers
  and their names, the session, the validators, a merge scratch, and an allocator. The
  Config passes its own, with its shared merge scratch; a checker passes its copy, with a
  merge scratch and validator list per check. So checks on several threads share nothing
  they write. The value below a changed one is located through the session's origins,
  which hold every layer's.
- **Allocation.** The checker's state, each check's temporaries, and a candidate text's
  tree come from the allocator given. The stack's trees stay the Config's, so the Config's
  allocator must outlive its checkers, as it must its snapshots.
- **A push with a checker's report.** It holds as any report does (D165). If the checker
  left validators out, the push runs them first, on the owning thread, over the stack
  merged with the candidate. Under `warnings_refuse` their warnings refuse, changing
  nothing.
- **Threads.** A checker needs atomic reference counts, the default (D20); the header
  says so.
- **Cost** (`bench/bench_layers.cpp`, shipping, release; the 9.8 KB overlay):

  | Row | Time |
  |---|---|
  | `checker()`, the owning thread's part | 0.2 µs |
  | `LayerChecker::check_text`, on any thread | 278–354 µs, as `check_layer_text` (385–388 µs) |
  | Push with the checker's report | 119–149 µs, as a held report from the Config |

  With `shape::validator`, which runs in the checker, the check is 364–466 µs and the
  push 202–259 µs.
- **Note 0003's constraints.**
  - The worker changes nothing the owner reads and takes no lock the owner takes.
  - Validators say whether they may run there.
  - Allocations come from the host's allocator, but for the stack's own trees, which are
    shared.
- **Tests** (`tests/data/test_layer_checker.cpp`):
  - a checker's reports equal the Config's, by text and by tree, including sources, origins
    and refusals;
  - it refuses includes;
  - it keeps the state it was made from after the Config changes, and after the Config is
    destroyed;
  - a worker checks 20 or more times while the owner pushes, sets and removes 50 layers,
    every report equal to the first. It is compiled only with atomic counts: CI's
    plain-refcounts job crashed in it, as the header says it would;
  - a push with a holding report runs only the left-out validator, and refuses on its
    warning, changing nothing;
  - a report from a checker made before a change does not hold.

  Locally, GCC's ThreadSanitizer finds no race in the data tests; CI's TSan job runs them
  too. `baker_alloc_tests` checks that a checker allocates through its allocator, returns
  all of it, and reaches no `operator new`.

## D168. A headless stack game paces its frames: `--frame-hz`

*Requirement note 0006 from the bridge integration, brought by the owner. A headless stack
game held open for a session of console requests (a tool's live preview, a harness driving
several games, an agent beside a person) spun its frame loop. The loop slept 1 ms per frame,
but each frame still ran ImGui's pass and the Noop renderer, so one core stayed near 100%:
four such games filled a four-core machine.*

- **`AppOptions::frame_hz` and `--frame-hz N`** cap the frames a second.
  - The loop sleeps out the rest of each period with `SDL_DelayPrecise`.
  - A frame that runs long starts the next period at once, rather than racing to catch
    up.
  - 0 is unpaced.
- **The default** paces a headless game run without `--frames` to `step_hz`, which is the
  game serving its console. Every other run is unpaced as before: a window presents at
  vsync, and `--frames` runs (tests, screenshots) run as fast as they can. So the play
  build is unchanged unless the person passes the flag.
- **The simulation is unaffected.** The fixed-step clock advances by elapsed time, so
  pacing changes when frames run, never how many steps a span of game time takes. The
  `sim` command does not use this loop.
- **Measured** (Duel, headless with a console, idle, 5 s, this container):

  | Run | CPU |
  |---|---|
  | `--frame-hz 0`, the old loop | 94% of a core |
  | The default, 60 Hz | 5% |
  | `--frame-hz 20` | 3% |

  A console request waits at most one period for its frame: the worst of 40 requests was
  16.7 ms at 60 Hz, against 0.4 ms unpaced. The note asked for tens of milliseconds.
- **Test.** `stacks/tools/test_commands.py` (the CTest `stacks_duel_commands`) starts
  Duel headless with a console on a free port. It sends a request, expects the answer,
  then measures the idle game's CPU over 3 s from `/proc` and expects under half a core.
  It measured 5%. Where there is no `/proc` (Windows), the CPU part is skipped.
- **Docs:** the flags table in `stacks/README.md`.

## D169. Columns on errors (E4)

*E4 of `dev/integration-plans.md`, the first step of plan B, which the owner started. An
editor underlines a span, not a line, and an agent fixing a long line needs the column
too.*

- **API** (`error.hpp`).
  - `Error::column()` is 1-based, in characters (code points) from the line's start, and
    0 when unknown.
  - `set_column()` sets it, and `set_location()` clears it, since a relocated error has a
    new line.
  - The record lives on the error path only (D55), so `Expected<T>` does not grow.
- **The printed form** (the owner's choice) is `file:line:column` when there is a column,
  the compiler convention that terminals and editors make clickable:
  `baker: unterminated string  (t.dta:2:6)`. Errors without one print `file:line` as
  before.
- **The unit** (the owner's choice) is characters, as people count and compilers report:
  a UTF-8 continuation byte does not count, and a byte-order mark is not a character. The
  editor server converts to the LSP's UTF-16 units itself.
- **Where columns come from.** A column is computed only when an error is made, from a byte
  offset already at hand:
  - **The lexer's errors** use the offset of the string, quoted symbol, comment, `$` or
    number they reject.
  - **The parser's errors** use the token's, the open bracket's (`is never closed`, `is
    closed by`, nesting too deep, `#remove` misused), or the directive's (unknown
    directives, `#else` and `#endif` without `#ifdef`, `#include` failures).
  - **Errors located by an array's line** have no column: a pack or origin declaration
    misread, a lookup, a shape error, a script call. That was E4's design. The editor
    widens these to the line's first token from edit-mode spans.
- **The plan's Acton note held.** A first version also stored the current token's offset
  per token, for the few sites that took only a line. It cost 5.3% of `Parser::run`'s
  instructions on `bench_parse` (cachegrind: 706.7M → 744.0M). Those sites now take an
  explicit offset, and nothing is tracked per token.
- **JSON.**
  - `baker.check/1.2` adds `column` to every problem; problems sort by file, line,
    column, then message.
  - `baker.reply/1.1` adds `column` to an error.
  - Both are minor versions, which old readers can ignore. Every writer, test, example
    regex and stack tool moved to the new names.
- **Cost** (`bench_parse`, release, this container, against a worktree of `main`, five
  runs each):

  | | Before | After |
  |---|---|---|
  | Throughput | 125.7–133.7 MB/s | 129.5–134.5 MB/s |
  | Instructions in `Parser::run` + `lex_token` | 1,413.2M | 1,405.4M |
  | Instructions, whole benchmark | 4,256.5M | 4,270.6M (+0.33%) |

  The whole benchmark's rise is outside the parser's hot functions.
- **Tests** (`tests/data/test_parse.cpp`):
  - each lexer error and parser error kind gives its line and column;
  - two-byte characters count once, and a byte-order mark does not count;
  - the printed form shows the column;
  - a lookup error and a pack declaration's error have none.

  The existing expectations moved to the new form and schema names.

## D170. Plan A's outcome, and objectives judged by their median

*Plan A of `dev/integration-plans.md` (an agent bridge over Baker's tool surface, in its
own repository) was evaluated in two rounds: 252 runs, two agent clients, headless and
live tasks on every stack. The owner brought the summary.*

- **Plan A is parked, as its kill criterion says.**
  - **Calls.** The bridge took 2 to 3 times the tool calls of the command line and skill
    in every group of runs: each client, headless and live tasks.
  - **Success.** It never did better on success beyond noise.
  - **What stays.** E1–E3 (D154–D157) stand alone and are kept. The command line, the
    skill and the loopback console serve the same agents.
- **Every failure, in every arm and client, was a Duel task.** All the other stacks'
  tasks passed every run. The failures were of one kind: the fix moved a Duel win rate out
  of its band on one seed of three.
- **Why.** Duel's win rates move from seed to seed. On clean data, `normal_vs_combo` runs
  from 0.28 to 0.49 over seeds 1–10, against a band ending at 0.5.
  - **The skill and `sim` disagreed.** The skill said one seed outside a band is noise and
    a shifted median is a finding. `sim` and `compare` failed on any seed.
  - **The evaluation's judge sided with `sim`.** It took `sim`'s exit status over seeds
    1–3, and agents split on which rule to follow.
- **The fix** (the owner's choice: judge win rates by their median).
  - **The form.** An objective may say `(over median)`. It is judged once per scenario,
    by its `pass` on the median of its values over the seeds run. That verdict decides
    `sim`'s exit status. Each run still records its own value and pass, for information.
  - **The record.** `sim` prints each median's verdict, adds `(median ...)` to the
    summary, and writes `medians.dta` beside the runs with `--out`. The JSON gains
    `medians`, and each objective result says `over`, so the schema is `stacks.sim/1.1`.
  - **`compare`** (`stacks.compare/1.1`) calls such an objective broken only when a
    scenario's median verdict went from pass to fail.
  - **Duel.** Its seven win-rate objectives say `(over median)`. The frame-data and
    reaction objectives stay judged per seed. Clean data passes on seeds 1–3, with
    `normal_vs_combo`'s median at 0.36.
  - **The skill** says `sim`'s exit status is the verdict, to be taken as it is, and that
    a median near a band's edge is a risk to report.
- **What the evaluation also found.**
  - **The headless CPU core** is D168.
  - **The journal's negative times** are D160.
  - **The skill's old loop** said only "`sim` over the same seeds", with neither every
    scenario nor `compare` (D159 added both after the evaluation's pinned commit). Its
    effect on success is not measured.
- **Tests.**
  - `stacks/base/tests/test_sim.cpp`: values one to five on five seeds, judged by an
    objective whose median passes and one whose median fails. A seed out of band does not
    fail its run. The verdicts read back from `medians.dta`, and `compare` reports a broken
    median verdict and ignores seeds moving.
  - The stacks' command test checks the new schema names.

## D171. A tweak whose path is a symbol (E6)

*E6 of `dev/integration-plans.md`, which comes before plan C; the owner chose to do it now.
`Tweak(const char* path, ...)` keeps the pointer, so the path must outlive the tweak. Code
writes literals, which do. A scripting host makes tunables from paths it builds at runtime,
and had no safe way to: GDScript's tunables in a Godot extension are the case plan C
names.*

- **`Tweak<T>(Symbol path, fallback, meta, registry)`** delegates to the `const char*`
  form with the symbol's interned text. That text is NUL-terminated and lives as long as
  the program, so nothing new is stored and `TweakBase` is unchanged.
- **Overloads.** A string literal still picks the `const char*` form. A `std::string_view`
  made at runtime converts to `Symbol` and is interned, so it is safe too. A `std::string`
  needs `Symbol(s)` written out.
- **The critique's point stands as documentation, not code.** Each new path stays
  interned, so a host makes tunables once, at startup or at a load, not in a loop. No
  owning string was built. `TweakMeta`'s strings (`desc`, `kind`, `group`) carry the same
  rule. The header and the cookbook say so, with `Symbol(text).str()` as the way to give
  them that life.
- **Test** (`tests/data/test_config.cpp`). Two tweaks get paths built in a
  `std::string`, one by `Symbol` and one by a view, and a description by a symbol's
  `str()`. The string is then overwritten and destroyed. The paths and description read
  back, a Config binds the tweak to its value with its source line, and destroying the
  tweaks empties the registry.

## D172. A project file for the game and its tools: `read_project`, `open_project`

*Plan B's Baker step (`dev/integration-plans.md` §3, critique B2), which the owner asked for
once the editor work began. A Config's layers, pack levels, include directories, and
defines were set up in each game's C++. A tool that opens the data (an editor checking as
you type, a CI check, an agent) needed a second copy, which drifts. The plan's kill
criterion was that the convention could not describe a stack without changes its authors
reject.*

- **The format** (`baker-project.dta`) is one `(project ...)` form holding `pack_levels`,
  `include_dirs`, `defines` (flags) and `layers`, bottom to top.
  - **Layers.** Each is `(name "path" [optional] [disabled])`.
  - **Paths** are relative to the project file's directory unless absolute.
  - **Strictness.** Every part but `layers` may be left out. An unknown key, a malformed
    layer, a stray flag or two layers with one name is a located error, so a typo is found
    rather than ignored.
- **API** (`include/baker/project.hpp`, in `baker::data`).
  - **`read_project(path, reader)`** returns a `Project` of `List`s. It reads through
    `reader` when given, which is how an editor serves an unsaved buffer. It reads text
    only, and refuses `#include`, so the project reaches no other file.
  - **`open_project(cfg, project, {.skip = ...})`** sets the session's pack levels,
    include directories and defines, then pushes the layers. `skip` leaves layers out by
    name: a simulation or check leaves out `local`, as the stacks always have.
- **The stacks.** Each of the seven has a `baker-project.dta` at its root describing the
  stack `base::Live` built in code: `default`, then `local` and `agent` optional; levels
  `engine game task`; `data` searched.
  - **`Live::open`** reads it from beside the data directory and opens it. It skips
    `local` when asked, as before.
  - **Without the file,** as in a packaged game whose data sits beside the executable, it
    builds the same `Project` in code.
  - **A project file's mistake** stops the load like data that does not parse: `check`
    reports it under `load`, located, and exits 2.
- **The kill criterion is not met.** The stacks' setup is described whole, and `Live`
  changed only in where it gets the stack. Its validators, tools, watcher and `#autorun`
  are as they were.
- **Not in the format:** script functions, validators and the reference file. Those are
  the game's code, or plan B's (the reference JSON comes from the game's build, B3). The
  editor session may ask for more in a requirement note.
- **Docs.** A cookbook section, "One project file for the game and its tools", and a line
  in `stacks/README.md`.
- **Tests.**
  - `tests/data/test_project.cpp`:
    - everything is read, with paths joined and flags set;
    - `open_project` sets the levels (a pack loads), the include directory (an `#include`
      resolves), the defines (`#ifdef PC`) and the layers, with the disabled one
      skipped;
    - `skip` leaves out `local`;
    - an unsaved buffer is read through a reader;
    - eight mistakes are refused, each located: the line, and the message names what is
      wrong.
  - `stacks/tools/test_commands.py`:
    - a layer named only in the copy's project file is in the game's stack (`check`
      finds its shape error);
    - a project file with an unknown key is a located load problem.
- **Core:** 13,518 → 13,664 lines.

## D173. The project file for tools: positions, every problem, `find_project`, the reference

*The editor session's requirement notes 0004–0007, after using D172's project file in its
server on every stack. Each asked for something every tool that shows or checks a project
would otherwise write for itself, slightly differently.*

- **Positions (note 0004).** Every entry `read_project` returns keeps where it is written,
  as Baker's errors give it: a 1-based line and a column in code points, a byte-order mark
  aside.
  - **Types.** `ProjectPosition {line, column}`, zero for a `Project` built in code.
    `pack_levels`, `include_dirs` and `defines` become `List<ProjectEntry>` (`value` and
    `at`). `ProjectLayer` gains `name_at` and `path_at`.
  - **How.** `read_project` reads with a `SourceMap`, which records each node's byte span
    and the file's text, and counts lines and code points up to a span's start. It is a
    load-time cost on a file of a few lines; nothing is kept per frame.
  - **Errors.** The format's own errors now carry a column too, at the form that is wrong.
- **Every problem (note 0005).** `ProjectOptions::go_on` pushes every layer that reads, and
  `open_project` returns a `List<ProjectProblem>` (the layer's name and its error) for the
  rest. It returns `Expected<List<ProjectProblem>>` in both modes:
  - without `go_on` (the game's default), the first error is the `Expected`'s error, as
    before;
  - with it, the problems are the value, empty when every layer read.
  - A layer that fails is absent, as a failed `push_layer` leaves it, and the layers above
    read with the macros and defines below. The problems' table is a `Vec` that allocates
    only for a problem.
  - `skip` now has a default member initializer, `std::initializer_list<...>()`, which is
    not list-initialization. Without one, GCC's `-Wmissing-field-initializers` flagged
    `{.go_on = true}`. A braced `{}` is what MSVC refuses (C2797).
- **`find_project(path, reader)` (note 0006).** It returns the nearest `baker-project.dta`
  (`kProjectFileName`) in `path` or a directory above it, or an empty `Symbol`.
  - **The search is lexical.** It tries `path` itself first, so a file or a directory may
    be given. It goes up to the top of `path` as written: the root for an absolute path,
    the directory it is relative to otherwise. It stops at `/` and at a drive root
    (`C:/`'s parent, `C:`, is relative).
  - **Existence.** Each candidate is tried through `reader` when given, with its text
    discarded, else with an `fopen`. That is the host's own check, so content that is not
    on the disk is found too.
- **The reference (note 0007).** `(reference "path")` is optional. It is joined with the
  project file's directory, and returned as `Project::reference` with `reference_at`.
  - Nothing reads the reference or asks that it exist: it is a build output, and the tool
    that reads it reports a stale or missing reference in its own terms.
  - `open_project` ignores it, as a game does.
  - D172 left it out of the format until a note asked.
- **The stacks.** `Live::open` names the file `kProjectFileName`. Its built-in `Project`
  uses `ProjectEntry` and designated initializers.
- **Docs.** The cookbook's project-file section gains the reference, positions, `go_on`
  and `find_project`.
- **Tests** (`tests/data/test_project.cpp`):
  - every entry's line and column, and characters counted after a BOM and before multibyte
    names;
  - `go_on`: a layer that does not parse and one that is missing are both reported, by
    name, the layer above them reads a macro from the one below, and a clean project
    returns no problems;
  - `find_project`: a file, a directory, a parent's project, through a reader, and none
    found for a relative or an absolute path;
  - the reference is joined and need not exist, and `(reference a b)` is a located error;
  - every format error has a column.
- **Core:** 13,667 → 13,765 lines.

## D174. The reference lists the objects a Context names

*The editor session's requirement note 0002. A tool that checks data without the game
learns its vocabulary from `baker.reference`, which had the functions and types but not
the objects added under a name. So `{clock thread_task ...}` looked like a call to an
undefined `clock`.*

- **`objects`** is a new field of the reference JSON, after `globals`, so the schema is
  `baker.reference/1.1`. It lists each live object that has a name, with its `type`:
  - the type's name, as `types` lists it;
  - for an object made from a script-only class (`{new Ghost attic}`), the class's name,
    as `script_classes` lists it.
- **Sorted by name.** It lists the objects present when the reference is written: for a
  build step, after the game's setup. The Context's own `scheduler` is one of them.
- **Cost.** `write_reference_json` walks the object slots, once, when it is called.
  Nothing else changes.
- **Tests** (`tests/script/test_reference_json.cpp`): two native objects and a script
  object, sorted, with an unnamed one left out; a removed object is gone from the next
  reference.
- **Core:** 13,765 → 13,784 lines.

## D175. Functions declared from a description: `Context::declare`

*The editor session's requirement note 0003. A tool that checks data without the game has
the game's reference but not its code. It could only stand in for a function with a C++
callable whose parameter types are fixed when the tool is compiled, so it registered
parameters of any type. That lost `check`'s type checks, and merged overloads that differ
only in types.*

- **API.** `Context::declare(name, std::span<const OverloadDescription>)` returns the
  `FuncRef`, on which the host sets the status and documentation.
  - An `OverloadDescription` holds its `params` and `result`.
  - A `ParamDescription` holds `type`, `kind` (`"value"`, `"raw"`, `"optional"` or
    `"rest"`) and `raw`. These are the reference JSON's own fields and names, so a tool
    passes them through as it reads them.
- **What it builds:** an ordinary native overload. The parameter table and type names are
  owned by the overload's `callable`, as `Overload::params` allows. Each type has the
  check the real parameter would have:
  - Baker's names map to the same `match` functions that `Param<T>` uses. Every integer
    name checks as `int`, `float32` as `float`, and `value` accepts anything.
    `unevaluated` and `values...` have no check; `$variable` and `array` have their own.
  - A host's own type (`vec2`) is checkable when a native function or method of the same
    Context takes it. `declare` borrows that parameter's `match`, so a tool that links the
    game's `Convert` types gets their checks. It finds them by walking the registered
    functions and methods when `declare` is called.
  - Any other name is a `Type` error naming the function and the type, with nothing
    registered. So are an unknown kind, a parameter after the rest parameter, and no
    overloads. Every overload is built before any is added.
    - The tool decides the fallback, for example declaring the parameter as `value`.
      `declare` never falls back silently.
- **A call fails** with a `Refused` error: the function was declared from a description
  and has no body to run.
  - The note offered "never run" or "fails when run". A tool that checks never runs it, so
    failing is the one behaviour needed.
  - A host that wants a body registers the native function.
- **Cost.** None on the call path. A declared function goes through the existing overload
  dispatch, and native functions are unchanged. `declare` is a tool-time call.
- **Tests** (`tests/script/test_check.cpp`):
  - five natives, and the same five declared as the reference describes them (an optional
    parameter, overloads differing only in type, a rest parameter, a raw parameter, a
    `$variable` and an array). Check gives the same ten errors over seventeen calls, and
    the two references write the same entries for them;
  - a call to a declared function fails;
  - a stub stays a stub in the demand;
  - a host type is refused until a native taking it is registered, then is checked;
  - a bad kind (in a second overload, with nothing registered), a parameter after the rest
    parameter, and no overloads are refused.
- **Docs.** `docs/reference-json.md`, "Checking without the game".
- **Core:** 13,784 → 13,922 lines.

## D176. Every type Baker's own functions use can be declared

*The editor session's requirement note 0008, from using D175 on the stacks. The reference
writes `name` for the path parameter of nine of the config tools (`config.get`,
`config.explain`, ...). `declare` refused it as an unknown type, so a tool could not
declare Baker's own functions from their reference. The editor declared them with `value`,
which loses the check.*

- **`name`** joins `declare`'s table of Baker's types. It checks as the config tools read
  one: a symbol or a string. `config_tools.cpp`'s comment on its `Name` type points at the
  table, so the two stay alike.
- **Every other type** the reference writes for a Context with the config tools bound was
  in the table already:
  - `$variable`, `bool`, `float`, `int`, `unevaluated`, `value` and `values...`;
  - results `bool`, `float`, `int`, `nothing` and `value`.
- **The round trip is a test.** It keeps this true as Baker's functions change.
  - The setup: a Context with the config tools bound writes its reference. The test reads
    every function back from the JSON (forty; thirty-two in a shipping build) and declares
    each under a `d.` name in a Context without the tools.
  - The calls: eleven argument shapes for each, against the natives and against the
    declared functions. `check` gives the same errors on the same lines, but for the name.
  - The one exception: `{set x 1}`, whose "needs a variable" rule check keeps for `set`
    by name.
  - Without the `name` entry, the test fails at the first config tool.
- **Not changed, from the session's observations:**
  - `ProjectOptions::skip` stays an `initializer_list`. A tool with a runtime list filters
    `Project::layers` itself, which is one loop.
  - The stacks' project files name no reference yet. Writing one is the stacks' build's
    job, and is left to the owner.
- **Core:** 13,922 → 13,926 lines.

## D177. Each stack's build writes its reference where its project file names it

*The editor session's requirement note 0009. After D176 the owner chose a build step for
the stacks' reference. The editor's server took one `--reference FILE` for every project
it checked, so a workspace holding several stacks checked calls in at most one.*

- **The build step.** `stacks_reference(game)` (`stacks/CMakeLists.txt`) adds a
  `POST_BUILD` command to each of the seven games. The command runs
  `stacks/cmake/write_reference.cmake`.
  - It runs `<game> reference --json` and writes the output to
    `<stack>/build/reference.json`, beside the project file and ignored by git.
  - The path is relative to the project file, not the build directory: the build directory
    is the developer's choice, and a tool knows only the project file.
  - Two build directories write the same vocabulary from the same source.
- **A game whose data does not load writes no reference.** The stale file is removed, so a
  tool reports the reference missing rather than checking against an old vocabulary, and
  the build goes on with a warning.
  - The command loads the data because script functions defined by `#autorun` are part of
    the vocabulary.
- **Rewrites.** The file is rewritten only when its text changes, so a tool watching it
  reloads only for a new vocabulary.
- **The file's shape** is the command's output: `{"reference": ..., "tweaks": ...,
  "shapes": ...}`, Baker's three documents in one object, as `--reference` was given it
  before.
- **Every stack's `baker-project.dta`** names `(reference "build/reference.json")`. The game
  ignores it (D173).
- **Tests.** `stacks/tools/test_commands.py`, `built_reference`:
  - the project file names the reference;
  - the build wrote it;
  - it is `baker.reference/1.1`, and lists the same functions as `reference --json` run
    now.
  - The failure path was tried by hand: a game that exits 1 removes the file and warns, and
    an unchanged reference keeps its modification time.
- **Docs.** `stacks/README.md`'s `--data` row.

## D178. A load lists the macros and script functions it defined, and where

*The editor session's requirement note 0010, from adding hover and completion. A tool
needs the data's own vocabulary as well as the game's: macros where a value goes, script
functions where a command goes, and go-to-definition on either. The session found a macro
by name but could not list them. A `{func}` was known only once its `#autorun` ran, which
a tool checking data must not do. The editor scanned file text for `#define`, `[` and
`{func`, which misses anything a macro makes.*

- **`LoadSession::macro_definitions()`** returns a span of `Definition {name, file, line,
  flag}`.
  - **What it records:** every macro definition the session's reads made, in the order
    read: `#define NAME` (a flag), `#define NAME (body)`, and a top-level `[NAME ...]`.
  - **Every definition is kept.** A name defined twice is listed twice, so a checker can
    report it. One `#undef`'d stays listed, and `is_defined` says which stand.
  - **Not listed:** macros a host defines in code, which are not the data's.
  - **Recording.** The parser's existing `defined()` hook, which already attributes a
    macro to its pack, pushes the entry. It is a new table in the session's state, so it
    is copied with the state and goes with the layer that brought it: removing a layer
    removes its macros from `read_session()`.
  - **Caches.** A recorded load's cache carries the definitions, as `(definitions (NAME
    file line flag) ...)` in its metadata, so a replayed load lists the same entries.
- **`script_functions(session)`** (`baker::script`) returns a `List<Definition>`.
  - **Where it looks:** every `{func NAME ...}` in the session's `#autorun` commands,
    nested ones included, at the command's file and line, in load order.
  - **Nothing runs.** The commands are already kept, expanded, with their locations, so a
    function a macro makes is found and nothing new is recorded.
- **Cost.**
  - **Parsing without macros is unchanged:** `bench_parse`, 4.2 MB, best of 10, two runs
    each against main. Main 33.2 and 32.3 ms, this branch 31.4 and 32.3 ms, with 43
    allocations either way.
  - **A file that is nothing but 40,000 macro definitions** (20,000 `[M ...]` and 20,000
    `#define F`, with a use of each) costs 1.3% more instructions: 1,741.6 M to 1,764.6 M
    under cachegrind, about 38 instructions per definition beside the define table's own
    insert. Time is within noise: best of 15, three runs each, main 14.5–16.2 ms, this
    branch 16.3–18.6 ms.
  - The note allowed edit mode only "if recording costs anything". The cost is small enough
    that every load records, so any Config's `read_session()` has the list.
- **Tests:**
  - `tests/data/test_preprocessor.cpp`: flags, `[NAME]`, `#define NAME (body)`, an
    included file's macro, a redefinition and an `#undef`, a host's define left out, and
    the same list from a cache replay;
  - `tests/script/test_reference_json.cpp`: four `{func}`s, at the top level, nested, from
    a macro, and one twice, with nothing run; and a removed layer's macros gone from
    `read_session()`.
- **Docs.** `docs/reference-json.md`, under "Checking without the game".
- **Core:** 13,926 → 13,983 lines.

## D179. Paths with a scheme are kept as written, and are absolute

*The baker-godot session's requirement note 0002, from plan C's runtime. Engines name
files through virtual roots: one for packaged content, one for per-user data that version
control never sees, others for mounted packages. A personal `local` layer belongs in the
per-user root, and the project file should be able to say so:
`(local "user://baker/local.dta" optional)`. Baker folded `user://baker/local.dta` to
`user:/baker/local.dta`, and joined the project file's directory onto it.*

- **Scheme paths** (`src/data/text.cpp`). A path that begins with a scheme, `name://`, has
  that prefix as its root.
  - **What counts as a scheme:** a letter, then letters, digits, `+`, `-` or `.`, at least
    two characters in all. So `C:/data/a.dta`, and even `C://data`, stay a drive.
  - **What follows:**
    - `is_absolute_path` is true, so a project file's directory is not joined onto it, and
      an include of one is not searched for.
    - `normalize_path` keeps the `//`, and drops `..` at the root as it does for `/`.
    - An include beside the file resolves under the same root (`user://baker/inc.dta`).
  - **Where the path reaches.** Every path Baker handles goes through these helpers, so
    the path reaches the reader, the writer and `Source::file` exactly as written.
  - **Unreadable paths.** A reader that cannot open such a path reports a missing file, so
    an optional layer is empty.
- **`parent_path` keeps a root.** A root is now its own parent: `/a` → `/`, `C:/a` → `C:/`
  (it was `C:`), `user://a` → `user://`.
  - `find_project`'s stopping rule becomes one comparison, `up == dir`, for every kind of
    root.
- **Tests:**
  - `tests/data/test_parse.cpp`: normalizing, `..` at a scheme root, parents, joins, and
    three near misses (a drive with `//`, a space, a leading digit);
  - `tests/data/test_project.cpp`: a project file below the root names a `user://` layer.
    It is not joined; the Config reads it and an include beside it through a host reader;
    `source()` names it as written; and an unreadable one is an empty optional layer.
- **Docs.** The cookbook's project-file section.
- **Core:** 13,983 → 13,993 lines.

## D180. A Config lists the files its stack was read from

*The baker-godot session's requirement note 0001. An engine or editor already watches files.
A host that knows which files matter calls `reload()` only when one changes, with the
save's time as `ReloadRequest::origin_ns`, so edit-to-see is measured from the save. A host
knew only the files it named itself, the layers' own files. An included file's edit was
found only by calling `reload()` on a timer, late, with no origin time.*

- **API.** `Config::stack_files()` returns a span of `StackFile {layer, path, present}`.
  `Config::stack_files_epoch()` returns a number that moves whenever the list does.
  - **What is listed:** each file layer's dependencies, bottom to top. That is its own
    file, every file its `#include`, `#merge` or `#require` reached, and every file one of
    them looked for and missed (creating that file would change the layer).
  - **Paths** are as the session's reader was given them, so a host maps them as it maps
    reads.
  - **Not listed:** a layer pushed as text, which has no files, and an overlay.
- **Where it comes from.** It is built from what the Config already kept. Each layer's
  `deps` were the files `reload()` re-hashes.
  - **When it is rebuilt.** It is flattened at every merge (`refresh_files`, beside
    `refresh_read`) into a `Vec` the Config owns.
  - **The epoch.** A merge that read the same files, in the same order, with the same
    presence, keeps the list and its epoch: a reload of an edited file does not move it.
  - **Reading it allocates nothing.** A host compares one number a frame.
  - **Shipping builds** list what their reads opened, like any other build. The note
    allowed an empty list there; nothing is saved by giving one.
- **Cost.** A merge now also compares its layers' dependency paths with the list.
  - **Instructions:** cachegrind over 400 merges (a 3,000-key base, with a 300-key overlay
    pushed and removed 200 times, tree layers with no files) gives 160,066,127 →
    160,094,355, about 70 instructions a merge, or 0.02%.
  - **Time:** `bench_layers`' `push_layer (tree), no policy` read 94.4 and 95.9 µs on
    main, and 100.9 and 101.5 µs on this branch. The instruction count says that gap is
    layout, not work. `push_layer_text` was 183.3/179.2 µs against 184.2/181.6 µs.
- **Tests** (`tests/data/test_config.cpp`):
  - a layer and its include, a missing optional layer, and a text layer with no files;
  - a reload that reads the same files keeps the epoch;
  - one that reaches a new include moves it, and lists the new file;
  - a missing layer file appearing moves it, and removing the layer drops its file.
- **Docs.** The cookbook's reload section, "Watching files yourself".
- **Core:** 13,993 → 14,027 lines.

## D181. `script_functions` takes the commands, so it serves a Config

*The editor session's requirement note 0011, from using D178. `script_functions(session)`
read `session.autorun()`. A Config reads each file layer with a session of its own, and
keeps the enabled layers' `#autorun` commands in `Config::autorun()`. Its `read_session()`
has only the top layer's, since each layer's session starts its commands afresh. So for
data loaded as a game loads it, the call found nothing below the top layer. The editor
walked `Config::autorun()` itself.*

- **The change.** `script_functions(std::span<const Node> autorun)` replaces the session
  overload: `script_functions(session.autorun())` or
  `script_functions(cfg.autorun().items())`.
  - **The same walk and result:** nested `{func}`s and ones a macro made, at the command's
    file and line, in load order, duplicates kept.
  - **Disabled layers.** A disabled layer's commands are not in `Config::autorun()`, and so
    not listed.
- **Tests** (`tests/script/test_reference_json.cpp`): two text layers' functions, in order,
  with the second's file and line; with the second layer disabled, one.
- **Docs.** `docs/reference-json.md` names both calls, and says why a Config's come from
  `autorun()`.
- **Core:** 14,027 → 14,026 lines.

## D182. A stack's game can offer its console write-back, when asked to

*The editor session's requirement note 0012. Its server shows a running game's live values
in hover, over the loopback console. It offers "write the live value here" through the
game's own `config.write_back`, so the edit is Baker's one-line diff with exact
provenance, not text the editor makes up. The stacks never bound the Config's write tools
(D156's opt-in): writing files stayed with the person at the panel. The owner chose an
opt-in for this as well.*

- **`--console-write`** (`AppOptions::console_write`, `LiveOptions::console_write`) makes
  `Live::open` bind `bind_config_tools(ctx, cfg, {.allow_write = true})`, so
  `config.write_back` and `config.promote` answer on the console.
- **Off by default**, as before: on only when the person starting the game asks.
- **Nothing else about the games changes.** The console stays on 127.0.0.1.
- **Tests.** `stacks/tools/test_commands.py`'s `console_write`, on a copy of Duel's data:
  - without the option, `config.write_back` after a `config.set` is refused, and
    `default.dta` is untouched;
  - with it, the live value is written to its line, and the file keeps its line count.
  - The helper that starts a headless game and connects is now shared with
    `headless_idle`. The console separates replies with blank lines, which the test
    skips.
- **Docs.** The `--console-write` row in `stacks/README.md`'s table.

## D183. Write-back keeps an integer token's form when the new value is whole

*The baker-godot session's requirement note 0003, from plan C's editor loop. Engines and
editors hold most tunable numbers as floats: an inspector's field, a script's float
property, a slider. A designer who tunes a value the data wrote as `96` expects the diff to
read `96` → `120`. It read `96` → `120.0`, which looks like a type change in review. Every
host whose live edits come from float fields meets this, so it belongs where the token is
replaced, which knows what the old token was.*

- **The rule** (`as_written` in `config_write.cpp`): when write-back replaces a token
  one-for-one, an old `Int` and a new `Float` that is finite, whole and within `int64`'s
  range is written as an integer.
  - **Where it applies:** `write_back`, `write_back_to` over an existing value, and
    `promote`, which all plan through the same replacement.
  - **Where it does not:** a new entry, a float token, a fraction, a NaN, an infinity, a
    whole value beyond `int64`, and a replacement that changes the number of values are
    written as before.
- **The same number writes nothing.** The comparison that skips an unchanged value now uses
  the value as it would be written, so `(lives 3)` tuned to `3.0` is not an edit.
- **Reads are unchanged.** The scratch value stays the float until the write-back re-reads
  the layer. A float lookup or tweak reads the same either way, and an int lookup now
  works too.
- **Tests** (`tests/data/test_config.cpp`), one promote over six paths:
  - a whole float over an integer becomes an integer;
  - over a float it stays a float (`3.0`);
  - the same value is not edited;
  - a fraction, `1e30` and an infinity are written as floats;
  - then `write_back` alike.
  - The corpus write-back test (every corpus value as one line) still passes.
- **Docs.** The write-back paragraph in `config.hpp`.
- **Core:** 14,026 → 14,036 lines.

## D184. Three tooling layers, written down

*The owner asked where the line falls between development tools in Baker, tooling outside
Baker, and the game itself, and whether there are three clear layers. There were, in
practice, but the rules were scattered (D153, D156, `INTEGRATIONS.md`, `stacks/agent.md`,
the integration plans), and each requirement note was placed by judgment.*

- **`dev/tooling-layers.md`** defines the three layers:
  - **Baker:** what needs Baker's meaning of the data, as data and hooks, with no I/O of
    its own and nothing paid until it is called.
  - **The host:** the operating system, the game's own code, and policy. The stacks'
    `base` is the reference host.
  - **External tools:** a separate process and repository that talks to a game only
    through the project file, the versioned JSON documents, the console and the game's
    commands.
- **The placement test**, three questions asked in order:
  - Would a second host have to copy Baker's rules? Then it is Baker's.
  - Does it need the operating system, the game's code, or a policy choice? Then it is
    the host's.
  - Otherwise it is the external tool's.
  - A need that seems to be both splits, as `stack_files` and the watcher that uses it
    did (D180).
- **Baker's own tiers**, as a table: always, shipping included; development builds; and
  writing on request (`allow_write`). The table lists the Config tools that bind in each.
- **An open question for the owner:** a host kit. The file watcher, the loopback console
  server and the tool commands' shared code touch the operating system, so they are not
  core. Every host rewrites them: the stacks, the Godot integration, and
  `examples/remote-console`. The note lays out three options:
  - leave them in hosts, which is what holds until decided;
  - an optional host-kit library beside the core;
  - the commands' shared code only.
- **Users' half.** `docs/tools.md`, linked from the README, gives the user-facing part:
  - the three parts and how to place a feature;
  - what Baker gives tools, tier by tier;
  - what the host does: watching through `stack_files`, carrying requests through
    `eval_request` on loopback, commands that need the game's functions, and policy;
  - how an external tool talks to a game.

## D185. Project options hold their skipped names

*The baker-godot session's requirement note 0004, from plan C's step 3: a fault in 0.1.0.
`ProjectOptions::skip` was a `std::initializer_list`, whose array lives only to the end of
the full expression that wrote it.*

- **The fault.**
  - **The failing code.** Options built in one statement and passed in the next read freed
    memory: chosen by a condition, kept in a member, or returned from a helper, as in
    `ProjectOptions o = skip_local ? ProjectOptions{.skip = {"local"}} : ProjectOptions{};`.
  - **Why it went unseen.** No compiler warned. A debug build usually read the stale array
    correctly.
  - **What it cost.** An optimized export's scenario runs kept the `local` layer they
    should have left out, so their results depended on the machine, which the option
    exists to prevent.
- **The fix.** `skip` is a `LayerNames`: up to eight `std::string_view`s held in the options
  themselves.
  - **Building it:** from a braced list, so `{.skip = {"local"}}` reads as before, or with
    `add(name)`.
  - **Lifetime:** the options are valid for as long as the strings the names view are.
  - **Cost:** nothing allocates.
  - **The default member initializer** is `LayerNames()`, not `{}`, which MSVC refuses
    to list-initialize (C2797). D173's comment about it goes.
- **Past eight names** a name is dropped, `add` returns false, and `open_project` refuses
  the options before changing anything (`Refused`). Opening a stack with a layer it was
  told to leave out would be the same fault again.
- **Tests** (`tests/data/test_project.cpp`):
  - options chosen by a condition in one statement and passed in the next;
  - options returned from a helper, both ways;
  - nine names refused, with nothing pushed.
  - Under ASan, the first case is the use after scope the old type allowed.
- **0.1.0 has the fault.** The next release's changelog says so: build the options inside
  the call until then, as the published documentation shows.

## D186. A host kit, `baker::host`, beside the core

*The owner's decision on D184's open question. Option 2: an optional library in this
repository, for the host code that every game rewrites.*

- **The first version holds four pieces:**
  - a watcher over `stack_files()` and the project file;
  - the loopback console, moved from `examples/remote-console`;
  - the live loop, from the stacks' `Live`, made general;
  - the tool commands (`check`, `reference`, `diff`, `promote`, `eval`), with hooks for
    the game's own functions and commands.
- **Shipping.** Under `BAKER_SHIPPING` the watcher and the console are inert. The live loop
  and the reading commands work.
- **Where it lives.** `baker::host`, in `src/host/` and `include/baker/host/`, beside
  `baker::shape`.
  - It is under the core's rules, and its public headers pass the same gate.
  - It has a 1,500-line budget of its own, and ships in every release.
  - `baker::baker` does not link it.
- **The plan** is `dev/host-kit-plan.md`: six steps, from the watcher to moving the stacks
  onto the kit, with tests, measurements and acceptance criteria. The stacks' JSON is kept
  byte for byte.

## D187. The host kit, step 1: the target and the watcher

*Step 1 of `dev/host-kit-plan.md` (D186).*

- **The target.** `baker::host` (`BAKER_BUILD_HOST`, ON) is built when `baker::script` is,
  and is not in `baker::baker`. Its pieces:
  - `src/host/` and `include/baker/host/`;
  - `tests/host/` and `baker_host_tests`.
  - Its headers are under the header gate: `check_headers.py` now walks subdirectories,
    and the header-alone test compiles `host/*.hpp` too.
  - `tools/count_lines.py` reports its budget, 1,500 lines, outside the core's. The core
    count does not change.
- **`baker::host::Watcher`** watches exactly what it is given: `watch(cfg.stack_files(),
  more)`, where `more` is the project file, say.
  - **Notifications on directories.** It watches each file's directory through the
    operating system, without recursion: inotify on Linux, `ReadDirectoryChangesW` on
    Windows. It reports only the files in the list, so a swap file, or a data file nothing
    reads, reloads nothing. That is the stacks' watcher, moved and narrowed.
  - **Stamps.** A file whose directory does not exist yet is checked by its write time and
    size, at most every 100 ms, as is every file on systems without notifications.
    - If a watched directory goes away, its files move to stamps.
    - A path with a scheme (`user://`) goes through the host's `StampHook`.
  - **What a poll gives:** `poll()` once a frame, then `changed()` and `newest_write_ns()`,
    on `file_clock_now_ns()`'s clock (since 1970), which a host also gives its Config as
    its time source.
  - **Watching again:** the host re-calls `watch` when `stack_files_epoch()` moves, and a
    directory already watched keeps its handle.
  - **It cannot fail:** a directory the system refuses falls back to stamps. So `watch`
    returns nothing.
  - **Under `BAKER_SHIPPING`** it watches nothing, and `poll()` never reports a change.
- **Tests** (`tests/host/test_watcher.cpp`, six cases):
  - an included file saved;
  - a swap file and an unused data file ignored;
  - a missing optional layer's file appearing;
  - the project file saved, and a file removed;
  - following the stack to an include in another directory after the epoch moves;
  - a directory that does not exist yet, by stamp;
  - the stamp hook for `user://` paths;
  - shipping inert.
  - **A quiet frame allocates nothing**, by the allocation counter, with a stamp check
    falling due within it.
- **One save may report more than once.** Windows reports one save as several
  notifications (the truncate, the write, the size), and they can complete in separate
  polls. The second report costs one more reload, which finds the content unchanged and
  does nothing. The tests collect every report of a save before they check for quiet,
  which CI's Windows jobs showed was needed.
- **Building on MSVC and libc++.**
  - The code that runs in one build only sits in an `if constexpr` block, not after an
    early `return`, which MSVC's `/W4 /WX` calls unreachable.
  - libc++ has no `clock_cast`, so there a file time reaches the system clock through the
    two clocks' offset now.
- **Next:** the console (step 2). The stacks keep their own watcher until step 5 moves them
  onto the kit.

## D188. The host kit, step 2: the console

*Step 2 of `dev/host-kit-plan.md` (D186).*

- **`baker::host::Console`** (`<baker/host/console.hpp>`) is `examples/remote-console`'s
  console, moved into the kit:
  - `listen(port)` on 127.0.0.1 only, with port 0 for any free port, and `port()`;
  - `poll()` once a frame on the Context's thread;
  - `clients()`.
  - **The two kinds of line:** a request, `(ID {command})`, gets `eval_request`'s one line
    of JSON; any other line gets the human reply.
- **The protocol is written down as it was.** Every reply is its lines, then an empty line.
  - The plan meant to drop the empty line after JSON replies. That would leave a client
    that reads up to it (the example's own) waiting forever, while keeping it costs a tool
    a skipped empty line.
  - `\r\n` ends a line too.
- **Changed from the example:**
  - **Pending output.** Each client keeps its unsent replies and the console sends them
    over the next polls. The example dropped a client whenever one `send` did not take a
    whole reply, which a large reply (2 MB in the test) does.
  - **No `SIGPIPE`.** On Linux, `send` passes `MSG_NOSIGNAL`, so a client gone mid-reply is
    not a `SIGPIPE` that ends the game.
  - **Winsock** starts and stops inside the kit, counted across consoles. A host no longer
    calls `sockets_start`/`sockets_stop`.
  - **The socket helpers** (`src/host/sockets.hpp`) are in `baker::host::net`, not the
    global namespace.
  - **One socket a console:** `listen` refuses a second time, and fails cleanly when the
    port is taken.
- **Under `BAKER_SHIPPING`,** `listen` returns false, so a shipped game opens no socket.
- **A quiet poll allocates nothing:** a connected client sending nothing.
- **Users.**
  - `examples/remote-console` links `baker::host`. It keeps a client-side copy of the
    socket helpers, since an example does not reach into `src/`, and its expected output
    is unchanged. Built for shipping, it says it opens no console and exits, and its test
    is registered only for other builds.
  - The stacks' app links `baker::host` and uses the kit's console. Before, it compiled the
    example's `console.cpp`. All 30 stacks tests pass, `test_commands.py`'s console cases
    included.
- **Tests** (`tests/host/test_console.cpp`):
  - a person's line, a tool's request, and an error;
  - a line arriving in pieces, and two lines at once;
  - two clients, and one leaving;
  - a 2 MB reply arriving whole;
  - listening once, and a taken port;
  - a quiet poll allocates nothing;
  - inert under shipping.
- **Docs.** The cookbook's console section and `docs/tools.md` name the kit's console, and
  the plan's note on the separator is corrected.

## D189. The host kit, step 3: the live loop

*Step 3 of `dev/host-kit-plan.md` (D186).*

- **`baker::host::Live`** (`<baker/host/live.hpp>`) is the stacks' `base::Live`, made
  general. It owns a Config, and `open(LiveOptions)` does the rest:
  - **Finding the stack.** It finds the project file with `find_project` from a path at or
    below the project's root, or takes a `Project` the host built (`built`), as a packaged
    game does. With neither it is a `NotFound` error.
  - **Opening it** with the host's `ProjectOptions`: `skip`, and `go_on`, whose problems go
    to `load_errors()`.
  - **Validators:** the check validator, and the shape validator when `baker::shape` is
    built (`BAKER_HOST_SHAPE`).
  - **Tools:** `bind_config_tools`, writing only with `console_write`.
  - **Autorun:** it runs every enabled layer's `#autorun`, then calls `changed`.
  - **Watching and the console:** it watches `stack_files()` and the project file, and
    serves a `Console` on `console_port` if one is given.
- **Each frame.** `begin_frame()`:
  - answers the console;
  - reloads with the watcher's newest save time as the origin;
  - watches again when `stack_files_epoch()` moves (a new include, a layer pushed or
    removed);
  - after any change to the stack (`on_reload_complete`), runs `#autorun` again and calls
    `changed`.
  - `end_frame()` marks the frame visible. The Config's clock is `file_clock_now_ns`, so
    edit-to-see runs from the save.
- **Generalized from the stacks:**
  - The stacks' `data/../baker-project.dta` rule is replaced by `find_project`.
  - Their `local`/`agent` layers and `packs/` directory are whatever the project file says.
  - Logging goes to a `Callback<void(const Error&)>` rather than a list of strings.
  - The host's `reader` and the watcher's `stamp` hook carry an engine's virtual roots.
  - `{live.loop}` stays the stacks' own: `config.loop` already gives the same numbers.
- **A changed project file is reported, not applied.** Re-opening the stack means removing
  every layer and resetting the session's levels, include directories and defines, which
  the Config does not offer as one step. The log says to restart; a reopen can follow if a
  host needs it.
- **Cost of a quiet frame.** It allocates nothing (the allocation counter, with a console
  and a watcher). It is 247 instructions in a release build, by cachegrind: 1,000 frames
  against 11,000 in a small driver, 2,636,614 against 5,106,634, with one console and three
  watched files. Its only system calls are the console's non-blocking `accept` and the
  watcher's non-blocking `read`.
- **Tests** (`tests/host/test_live.cpp`, seven cases):
  - opening from a path below the root, with `#autorun` and the tools working;
  - a built stack, and none at all;
  - a broken layer stopping `open`, and reported with `go_on`;
  - `skip`;
  - a saved file reloaded with its origin, and `#autorun` redefining a function;
  - a new include in another directory watched from the next frame on;
  - a changed project file reported;
  - the console answering, refusing `config.write_back` unless `console_write`, then
    writing the line;
  - a quiet frame allocating nothing;
  - shipping opening the stack with no console.
- **The stacks still use `base::Live`.** Step 5 moves them onto the kit.

## D190. The host kit, step 4: the commands

*Step 4 of `dev/host-kit-plan.md` (D186).*

- **`<baker/host/commands.hpp>`** gives a game's binary the tool commands the stacks run:
  `check`, `reference [--json]`, `eval [--seed N] [--json] "{command}"...`,
  `diff [--layer L]` and `promote [--from L] [--to T] [--write | --patch FILE]`, each with
  `--project PATH`. They live in the game's binary because only it has its script
  functions; an editor, a CI gate or an agent with a shell calls them and reads Baker's JSON.
- **The command line.** `parse_command_line` reads the command, the kit's flags and the
  `{...}` arguments into a `CommandLine` of views into argv. Everything else goes to
  `rest`, in order, so the host reads its own flags (a scenario file, say) from there. A
  flag missing its value, or a seed that is not a number, is an error.
- **What the game gives** (`CommandHooks`):
  - `bind` and `unbind`, its script functions on a fresh Context;
  - `changed`, its read of the structured data, as Live's;
  - `reset`, its state from a seed, before `eval` runs;
  - `extra_trees`, files it wants checked beside the data, or the located problem that
    stops them reading;
  - `built` and `open`: the stack without a project file, and how it opens (a check
    leaves out one person's `local` layer).
- **`run_command`** returns the exit status: 0, `kCommandFailed` (1) when something failed,
  `kCommandError` (2) when it could not run. It returns -1 for a command that is not the
  kit's, so the host runs its own; `command_help()` gives the lines for its usage text.
- **Each command** opens the stack through `Live` without watching, so it reads exactly
  what the running game reads.
  - `check` writes baker.check/1.2. A call to an undefined function fails it (a typo, in
    a tool's or an agent's edit) and is named on stderr. The shapes are checked over the
    merged data when `baker::shape` is built. Data that does not load is still a report,
    with exit 2.
  - `reference --json` writes Baker's three documents as one object:
    `{"reference", "tweaks", "shapes"}`.
  - `eval --json` writes one baker.reply/1.1 line a command, numbered from 1.
  - `promote --patch` writes the change as a unified diff for `git apply`, and nothing
    else. Its names are relative to `patch_root`, else to the project file's directory.
- **Shipping.** `diff` and `promote` refuse with exit 2; the others run.
- **Tests.** `tests/host/game.cpp` is a small game binary over the kit, with a script
  function, a global the reset sets, a `--scenario` flag through `rest` and
  `extra_trees`, the `local` layer skipped, and a command of its own. Its stack is
  `tests/host/stack/`. `tests/host/test_commands.py` (ctest `baker_host_commands`) runs:
  - check: clean data, an undefined call, a value out of its shape's range, a layer that
    does not load, the game's extra trees, and the skipped layer;
  - reference, as Markdown and JSON;
  - eval: on the game reset with the seed, and an error as a reply;
  - diff, `promote --patch` (one line, its comment kept, nothing written) and
    `promote --write`;
  - the game's own command, an unknown command, a bad flag;
  - under shipping, the refusals.
- **Size.** `baker::host` is 1,231 lines of its 1,500, from 773.
- **The stacks' commands are unchanged** until step 5 moves them onto the kit; their
  `test_commands.py` then compares the JSON byte for byte.

## D191. The watcher maps virtual paths to disk files; every live edit moves the epoch

*Requirement notes 0005 and 0006, from the Godot integration.*

- **Mapping a host's paths to the disk (0005).**
  - **The need.** Engines name files through virtual roots (a project root, a per-user
    root) that are ordinary directories during development. A host whose stack names its
    layers that way had to route every path through the stamp hook, so each file was
    polled at most every 100 ms and the system's notifications went unused.
  - **The hook.** `Watcher::set_disk_path_hook(DiskPathHook)`, where `DiskPathHook` is
    `Callback<bool(std::string_view path, TextSink out)>`, the shape of the session's
    reader. It writes the file on the disk a path is, or declines (a packaged archive's
    file has none). A declined path goes to the stamp hook, or to the disk as given, as
    before.
  - **When it runs.** It is asked in `watch()`, never in `poll()`, so a quiet frame still
    allocates nothing.
  - **What is reported.** The watcher names a change by the path the host gave (in
    `changed()`, and so in sources and reload records), not by the disk path.
  - **In Live.** `LiveOptions::disk_path` passes the hook to Live's watcher, beside
    `reader`.
- **Every live edit moves `Config::epoch()` (0006).**
  - **What was wrong.** The epoch moved only when the Config merged, so no `set()` moved
    it: neither the first one into a path, which adds a scratch entry without a merge, nor
    the fast path. The comment said only the fast path was left out.
  - **Now.** Both increment it: one increment, with no allocation and no other work on
    the fast path. `clear_scratch`, write-back, `promote` and `restore` already merged,
    and so already moved it.
  - **What a host does with it.** It compares `epoch()` once a frame to tell that what it
    shows or caches is stale. That covers edits it did not make: a tool's
    `{config.set ...}` through the console, or a bound tweak's.
  - **Cost.** `bench_cache`'s fast-path set, three runs each (two samples a run), on the
    same machine against a worktree of main: before, 60.8 to 68.4 ns; after, 61.8 to 67.1
    ns. That is within the noise.
  - **Independent of the journal.** It moves whether the edit journal is on or off. Under
    `BAKER_SHIPPING`, where live edits are refused, they do not move it.
  - **Telemetry is unaffected.** Each `ReloadRecord` keeps the epoch after its own change,
    and `mark_visible` matches records by their stored epoch, so edits between a reload
    and its frame do not disturb it.
- **Live's `changed`.** `begin_frame()` calls `changed` once when the epoch moved without
  a change to the stack, however many edits there were. It notes the epoch after `changed`
  and `#autorun` return, so a value they set themselves does not call `changed` again.
- **Tests.**
  - The watcher: a `res://` path mapped to a temporary file is watched by notification
    (`by_stamp()` counts only the archive's file), and its change is reported by the
    `res://` name.
  - The Config: every kind of live edit moves the epoch with the journal off, and taking a
    snapshot does not. The existing fast-path test now expects one increment.
  - Live:
    - `changed` fires once a frame for edits made directly, through the fast path and
      through script, and not on a quiet frame;
    - a stack named by `res://` paths, read through the reader and watched through the
      disk-path hook, reloads on a save, and its source still names `res://`.

## D192. The host kit, step 5: the stacks run on the kit

*Step 5 of `dev/host-kit-plan.md` (D186).*

- **`base::Live` is now the stacks' conventions over `baker::host::Live`.** The kit's Live
  leaves those conventions out on purpose (plan §3.3), so the stacks keep them in a
  wrapper:
  - **The stack.** `data_stack(data_dir)` gives two things. One is the path the kit finds
    `baker-project.dta` from: the data directory, when the file is beside it, so a broken
    project file is still reported as `baker-project.dta`. The other is the built stack
    (`default`, optional `local` and `agent`, pack levels `engine`, `game`, `task`) for a
    packaged game without one.
  - **Leaving out `local.dta`** for a simulation or a check.
  - **`{live.loop}`.**
  - **The log as lines** for the panels and stderr.
  - The class keeps its interface, so the app, `sim` and the stacks' Live tests are
    unchanged. It is 202 lines against 252.
- **`base::Watcher` is gone.** The kit's watcher watches exactly the files the stack read,
  packs and includes among them, where the stacks' watched the data and packs
  directories. Its tests went with it; the kit's own watcher tests cover the same cases,
  on Windows too. The `Wine` note in the base README went with it.
- **The console comes through Live.** `base::LiveOptions::console_port` passes the port to
  the kit, which serves the console from `begin_frame()`. The app prints the port it
  listens on, as before.
- **The commands.** `base::main` now parses the line with `parse_command_line`. It reads
  its own flags (`--data`, `--sim`, `--metrics`, `--out`, `--scenario`, `--seeds`,
  `--step-hz`, compare's directories) from `rest`, and refuses anything else, as before.
  `sim` and `compare` stay the stacks' own; everything else goes to `run_command`:
  - **The hooks:** the `Game` (bind, unbind, load, reset); `extra_trees` (the scenarios and
    objectives check reads, an objective that does not load being a located load
    problem); the built stack; and `local` left out.
  - **Paths:** the data directory is the project path and the patch root, so a patch
    still names `default.dta` and applies from `data/`.
  - `tools.cpp` is 301 lines against 576: check, reference, eval, diff, promote and the
    unified diff moved out.
- **Changes a person sees:**
  - `diff`'s heading reads `layer 'agent' changes:` where it read `agent.dta changes:`.
  - `promote --write` ends `layer 'agent' is unchanged`.
  - An objective file that does not parse now gives check's JSON with the problem under
    `load` (exit 2), where it printed only the message.
  - `diff --layer` and `promote --from` name another layer.
  - The kit's `command_help()` no longer names `--project`. A host names its own location
    flag; the stacks' is `--data`, and the kit's test game prints `--project` itself.
- **Tests.** All 30 stacks tests pass unchanged, `stacks_duel_commands`
  (`test_commands.py`) included.
- **The same bytes.** The duel from main and from this change, Release builds, each on its
  own copy of the stack, ran the same 13 cases: check (clean, an agent layer, an undefined
  call, a shape error, a broken file), reference and `--json`, eval and `--json`, diff,
  promote, `promote --patch` and `sim --json`. Exit status, stdout, stderr and the patch
  are identical, except `diff`'s heading.
  - **A gap the comparison found.** The reference lost `{live.loop}`: `base::Live` bound it,
    and the kit's commands do not go through `base::Live`. The stacks now bind it in the
    commands' `bind` hook too, through one `bind_live_loop`.
- **Edit-to-see on the cube.**
  - **The method.** A Release build, headless at 60 frames a second, with 30 saves of
    `local.dta` 250 ms apart, read through `{live.loop}`. Three runs, before and after.
  - **Medians:** before 6.8, 11.2 and 3.8 ms; after 3.8, 7.0 and 7.7 ms.
  - **Worst:** about 17 ms in every run, one frame.
  - **Reading it.** The spread is which part of a 16.7 ms frame a save lands in; the move
    changed nothing measurable.
- **Size.** The stacks' live, watcher, tools and app files are 803 lines, from 1,397.
  `baker::host` is 1,251 of its 1,500.

## D193. The console's sockets send at once; `config.clear` drops one live edit

*Requirement notes 0013 and 0014, from the editor integration.*

- **Pipelined requests (0013).** The note measured 60 pipelined requests at 48–60 ms on
  the Duel stack. It describes a console that sends each reply as its own small write,
  which Nagle's algorithm holds for the client's delayed acknowledgement. That was the
  example console the stacks used before the host kit (D188). The kit's `Console` already
  answers every complete line of a poll and sends the replies in one write.
  - **Measured on the kit's console.** Duel, Release, headless at 60 Hz, ten batches of
    `config.explain` requests. Sent in one write: 60 or 200 requests take 16 to 17 ms,
    one frame; 1,000 take 14 to 20 ms; 4,000 take 18 to 38 ms. Sent as one write each,
    60 or 200 take 33 ms, two frames. That delay is the client's own Nagle: its second
    write waits for the first one's acknowledgement, and the requests land in two polls.
  - **`TCP_NODELAY` on each client socket** anyway (`net::set_nodelay`, at accept), as the
    note proposes. Then a later poll's write never waits on the acknowledgement of an
    earlier one: the case of a batch whose replies outrun the socket's send buffer and
    finish over several polls, or a client that holds acknowledgements longer. On Linux
    loopback it measured no change (1,000 requests 15 to 20 ms against 14 to 20; 4,000, 18
    to 38 against 24 to 36), since a client that keeps reading keeps acknowledgements
    flowing. It costs nothing without a client.
  - **No test.** A timing test would be as noisy as these numbers.
- **`{config.clear PATH}` (0014)** drops one path's live edit, as `clear_scratch(path)`
  does, so the value is the files' again, and the journal records the edit reverted.
  - **What it returns.** 1 if there was an edit to drop, 0 if not; a path with no live
    edit, or no value at all, is not an error.
  - **No edit, no change.** A path with none changes nothing: `clear_scratch` would merge
    and move the epoch, so the tool looks in `unsaved()` first.
  - **No path, no wipe.** Clearing all of scratch is not offered; `{config.clear}` with no
    path is an arity error, so a tool probing whether the command exists cannot drop every
    edit.
  - **Where it is bound.** In development builds, beside `config.set`, and not behind
    `allow_write`, since it writes no file.
  - **Its test:** dropping one of two edits, the journal's `reverted`, 0 twice with the
    epoch unmoved, and the bare call failing with the other edit kept. The shipping test
    checks it is not bound.

## D194. The host kit, step 6: docs, the example, the integrations

*Step 6 of `dev/host-kit-plan.md` (D186); the plan is done.*

- **`examples/host`** is the acceptance's small host: one `main` of 64 lines, comments
  included.
  - **Commands.** With a command it runs the kit's commands, with `bind` its only hook;
    `play [--frames N] [--console-port N]` is its own command.
  - **Play.** It runs `Live` with the console, and its `changed` prints a tweak and how
    many times the data's `#autorun` called the game's `spawn`.
  - **Its stack:** a project file, a default layer with a shape, and an optional `local`.
  - **Tests.** Two CTest cases: `check` writes a passing report, and three frames of
    `play` print `player speed 4.5, 3 spawned`. Both run in shipping builds too, where the
    stack still opens and no console listens.
  - **A lesson it taught.** Its first `spawn` printed to stdout, which put a line ahead of
    `check`'s JSON. A game's functions must keep stdout clean for the commands, so the
    example counts instead, and `docs/tools.md` says a game's functions must print nothing
    to stdout while a command runs.
- **`docs/tools.md`** has a section on the host kit:
  - what it is, how it is built (`BAKER_BUILD_HOST`), and what shipping turns off;
  - a short part for each piece: `Live`, `Watcher` (with the disk-path and stamp hooks),
    `Console`, and the commands, with a table of them;
  - "What the host does" now says each duty has a ready-made form there.
- **Elsewhere in the docs:**
  - **The cookbook's watching-files bullet** points at `Live` first, then says how to
    watch the files yourself.
  - **The README** lists `baker::host` among the targets, `BAKER_BUILD_HOST` among the
    options, and the kit among the features.
- **The integrations are told** what they can drop once 0.2.0 ships:
  - **The editor session:** a game that links the kit serves its console and its commands
    the same way the stacks do, so the server can count on `config.clear` and on prompt
    pipelined replies.
  - **baker-godot:** it can replace its own watcher with `Watcher` and the disk-path hook
    (0005), its console with `Console`, and its change tracking with `epoch()` (0006),
    keeping its own file access through its reader.

## D195. The kit's commands read through the host's reader and write where it says

*Requirement note 0007, from the Godot integration.*

- **The need.** An engine runs the game's own binary as its tool, but names its files
  through its own roots and owns the process's stdout: it prints its banner and messages
  there. The kit's commands read the stack from the disk alone, and wrote only to stdout.
- **`CommandHooks::reader`** is the same `LoadSession::Reader` Live and the Config take. The
  commands open the project file and the layers through it, and `promote --patch` reads the
  text it compares against through it, so a stack named under a virtual root reads the
  same in a command as in the game. A patch's names are still relative to `patch_root`;
  with a virtual root, a host sets `patch_root` to the directory on its disk the patch
  applies from, or the "apply it from" hint names the root (`game:`).
- **Where the output goes.** In order:
  - `--output FILE` (`CommandLine::output`) writes it to a file; a failure to write it is
    exit 2, as a patch that cannot be written already was.
  - Otherwise `CommandHooks::output`, a sink the host routes.
  - Otherwise stdout, as before.
  - Messages for a person still go to stderr.
- **How.** `run_command` gathers the output while the command runs (`out` appends to a
  thread-local buffer) and sends it once the command is done. The exit statuses are as
  they were. The commands' output is small (a report, a diff, a reference), so gathering
  it costs nothing that matters.
- **Why `--output`, not `--out`.** The note's integration uses `--out FILE` of its own,
  and the stacks already read `--out DIR` for `sim`. The kit's flag must not take a name
  hosts use, since `parse_command_line` consumes it before the host sees `rest`.
- **Tests** (`tests/host/test_commands.py`, a new case; the test game has `--root DIR`, a
  reader under `game://`, and `--to-stderr`, a host sink):
  - check reports a shape error located in `game://data/agent.dta`;
  - eval reads through the reader;
  - `promote --patch` compares against what the reader reads;
  - `--output` writes the report with stdout empty, and exits 2 for a path that cannot be
    written;
  - the host's sink gets eval's output.
  - The 30 stacks tests pass unchanged.

## D196. A symbol literal interns once per literal

*From a reading of the book Game++ (sections 0x21–0x27): interning a literal at every
use is the string-pool trap.*

- **What was wrong.** `"price"_sym` called `Symbol(std::string_view)` on every
  evaluation: the table's mutex, a hash and a probe. On `bench_lookup`'s 20-child array
  that is 17 ns a lookup, beside 6.5 ns with a Symbol held in a variable. Code written
  the natural way paid it every frame, and so did an off-thread `LayerChecker` (D167)
  queueing on the same mutex.
- **Now.** `operator""_sym` is a literal operator template over `detail::SymbolLiteral`
  (C++20 class-type template arguments). Each distinct literal is one instantiation,
  which holds its Symbol in a function-local static, interned on the first call (one
  static per distinct text, shared by every site that spells it); after
  that a call is the static's guard and one load. Call sites are spelled as before.
  `Symbol(text)` still interns on every call, for text made at run time.
- **Measured** (`bench_lookup`, release, this container, best of 7 × 2M calls):

  | row | before | after |
  |---|---:|---:|
  | `find_float(key)`, a held Symbol | 6.47 ns | 6.5 ns |
  | `find_float("key19"_sym)` | 17.03 ns | 9.30 ns |
  | `find_float("key19")`, by `std::string_view` | 17.09 ns | 18.33 ns (noise) |

  The 2.8 ns the literal still costs over a held Symbol is the guard check; a loop that
  looks up the same key millions of times should still hold the Symbol.
- **Compilers.** GCC 13, Clang 18 and MSVC 2022 accept the form; the matrix (C++23,
  EASTL, shipping, sanitizers) and CI (MSVC, libc++, TSan) are the check.
- **Tests.** The row above is in `bench_lookup`; the existing tests use `_sym`
  throughout and exercise it.

## D197. The symbol table reads without its lock

*From a reading of the book Game++ (sections 0x40–0x47, and a parse profile prompted by
0x20): remove the shared point rather than lock it faster.*

- **What was wrong.** Every `Symbol(text)` took the table's mutex, to read as well as to
  insert. Under callgrind a text parse spent 13% of its instructions interning (7.4% hashing
  and probing, 4.2% in the mutex, 1.4% comparing), and with the symbols already interned
  from the previous parse. From threads, interning serialized: 24 ns alone, 80 ns a call
  with two threads, 193 ns with four (`intern_threads` driver, this container).
- **Now.** The table is append-only and its entries immutable, so a read needs no lock:
  a reader loads the current table, probes it, and either finds the entry or reaches an
  empty slot; only then does it take the mutex, probe again (another thread may have
  inserted it) and insert. Growth builds the new table under the lock and publishes it
  whole; the old table is leaked, since a reader may still be probing it, and geometric
  growth bounds the total. Slots are `std::atomic` pointers with acquire loads and release
  stores; an entry is complete before the store that publishes it.
- **Measured** (release, this container, best of 10 or 7, three runs each):

  | | before | after |
  |---|---:|---:|
  | `bench_parse`, 4.2 MB of config text | 200–202 MB/s | 207–209 MB/s |
  | `bench_lookup`, `find_float("key19")` by text | 17.1–18.3 ns | 14.6 ns |
  | `bench_lookup`, `find_float("key19"_sym)` (D196) | 9.3 ns | 9.0 ns |
  | `Symbol(text)` of an interned name, 1 thread | 23.7 ns | 8.4 ns |
  | the same from 2 threads, per call per thread | 80.5 ns | 6.9 ns |
  | the same from 4 threads | 193.3 ns | 7.3 ns |

  Allocations per parse are unchanged (43). The table grows by leaking the old slots
  array: for a program that interns 100,000 names, about 3 MB of slots over the lifetime,
  against the names' own 64 KB blocks that were never freed either.
- **Tests.** `test_core`: four threads intern shared and private names while the table
  grows; shared names are one symbol, every name reads back; TSan in CI runs it.

## D198. A tweak's callbacks may change the Config, and cannot loop

*From a reading of the book Game++ (sections 0x2D and 0x55): a listener that changes the
list while it is walked, and listeners that notify each other back.*

- **What was wrong.** `TweakBase::notify` walked `on_change` by reference. A callback that
  added a callback to the same tweak moved the table under the walk (undefined behaviour,
  though no test did it). A callback that set a tweak whose callback set this one back
  recursed until the stack ran out; the header said nothing about either.
- **Now.** `notify` walks by index over the count at entry, so a callback added from
  inside one runs from the next change; and a tweak being notified is not notified again
  until its callbacks return, so two tweaks whose callbacks set each other each notify
  once. The value still changes; only the second notification is elided. One `bool` in
  the tweak's `Extra`, which only tweaks with callbacks have; one branch in `notify`, which
  runs on a change, never on a read.
- **Reload callbacks** already ran over a copy (`on_reload`) or by index with the count
  re-read (`on_reload_complete`); a test now says so.
- **Tests** (`test_config`): a callback sets another tweak, which notifies once; two
  callbacks set each other and each runs once; a callback added from inside runs next
  time; a reload callback removes itself and another while notified, and a completion
  removes itself.

## D199. The lexer's atom-end test is a table

*From a reading of the book Game++ (section 0x20): a class table beats a chain of
compares for a byte scan.*

- **What changed.** `is_atom_end` (`src/data/text.hpp`) tested nine conditions a byte;
  it indexes a 256-entry `constexpr` table. `lex_atom`'s loop is one load and a branch a
  byte.
- **Measured** (`bench_parse`, 4.2 MB, release, this container): 207–209 MB/s before
  (after D197), 209–213 MB/s after, three runs each; under cachegrind the parse runs
  4,072,722,831 instructions before and 3,926,411,030 after, 3.6% fewer. Allocations
  unchanged.
- **What remains** in the lexer, from the callgrind profile that found this: `lex_token`,
  `lex_atom` and `classify_number` are 40% of a parse, `classify_number` scanning each
  atom a second time. A one-pass lexer that classifies while it scans is the next
  experiment, and a larger change.

## D200. A new watch list keeps an edit made since the last poll

*Requirement note 0008, from the Godot integration.*

- **What was wrong.** `Watcher::watch()` took a fresh stamp of every file. An edit made
  between the last poll and the new list counted as already seen, and the live loop never
  reloaded it until the next save. The list changes exactly when someone is busy with the
  files:
  - a write-back creates a missing layer's file, then the person edits it;
  - an `#include` is added and the included file saved a moment later;
  - a tool writes several files at once.
  It shows first on a fresh machine, where a per-user layer's file and its directory do
  not exist when the game starts.
- **Now.**
  - **A file that stays in the list keeps the stamp last seen of it.** If the file differs
    from that stamp now, it is marked pending and reported at the next poll, with its write
    time as the origin. This covers a file whose directory appeared, so that it moved from
    stamps to the system's notifications, and a path the stamp hook answers for.
  - **A file new to the list is stamped as now.**
  - **The stamp follows what was reported.** A file watched through notifications updates
    its stamp when a change to it is reported, so the comparison is against what was
    reported, not against what `watch()` saw. A change both pending and notified is
    reported once.
- **Cost.** The comparison happens in `watch()`. `poll()` checks one flag before its usual
  work, and a quiet frame still allocates nothing, per the existing test.
- **Tests** (`test_watcher`, one case; both subcases fail on the previous watcher):
  - a stamped file whose directory appears with its first save between two lists is
    reported, and a file new to the list is not;
  - a hooked path changed between the last poll and a new list is reported at the next
    poll, once, with its write time.

## D201. The Game++ reading's designs: instruction counts, threading docs, drop-in layers

*The designs of the Game++ reading that D196 to D199 did not build, with the owner's
decision on each.*

- **Instruction counts: a local tool, not a CI gate.** The reading found cachegrind's counts
  deterministic, run to run. But counts move with the compiler, so a committed baseline would
  break whenever a CI runner's compiler is upgraded. Instead:
  - **`tools/count/`** is a small CMake project, a driver of fixed workloads built against any
    Baker tree, so both sides run the same driver source. The workloads:
    - `parse`: 16 KB of config text;
    - `lookup` and `lookup_text`: `find_float` on a 20-child array, by Symbol and by text;
    - `set`: `Config::set`'s fast path;
    - `frame`: a quiet frame of the host kit's `Live`.
  - **`tools/count_instructions.py`** builds the driver against a base commit, in a worktree,
    and against the working tree. It runs each case under cachegrind with N and 2N operations
    and prints the difference over N as a Markdown table.
  - **Deterministic.** On this commit against itself, every case is identical (parse
    1,849,845; lookup 203; lookup_text 365; set 631; frame 197 instructions per operation).
    A run takes about a minute.
  - **A lesson from building it.** The first lookup case read its array with `find_array`,
    which returns the value after the tag, so every lookup took the error path and counted
    1,657 instructions. The driver now checks each case's first lookup and refuses to run
    if it fails. Natively the corrected case takes 14 ns a lookup, against
    `bench_lookup`'s 11 ns for the same key.
  - **CLAUDE.md's Benchmarks rule** now names both tools.
- **Threading docs.** The cookbook's new "Threads" section collects what was said only in
  headers:
  - read through references;
  - copy a Node to keep it, on the game's thread: one count per load contends across
    threads, as the reading measured: 11.6 ns a copy on one thread, 85 on two, 158 on four;
  - a reload is the frame's swap point: a raw reference across it dangles, and a `Ref`
    keeps the old tree;
  - tweaks are read atomically one by one, not as a set;
  - checks off the game's thread take `checker()`;
  - symbols may be made from any thread;
  - `set_stack_budget` on a thread with a small stack.
- **Drop-in layers: deferred.** `dev/drop-in-layers-note.md` stays as the proposal, marked
  deferred. The explicit list (the project file, `#include`, `push_layer`) remains the only
  way a file joins the stack, until a host or an integration asks with a requirement note.
  No one has asked, and the note's own main risk is a stray file joining the game.
- **The one-pass lexer** is run as an experiment, with the bar the reading set: kept only if
  a parse runs at least 10% fewer instructions, with an agreement test holding its number
  grammar to `classify_number`'s. Its D-entry gives the result either way.

## D202. The one-pass lexer: not built, its ceiling is under the bar

*The experiment D201 started, from the Game++ reading: classify numbers during the atom
scan instead of in a second pass, kept only if a parse ran at least 10% fewer instructions.*

- **The profile first** (callgrind, `tools/count`'s `parse` case: 200 parses of 16 KB of
  bench_parse's config text, RelWithDebInfo). Each function's own instructions:
  - `lex_token`: 13.2%, of which the whitespace and comment skip is about 3.8%, about ten
    instructions a byte of whitespace;
  - `lex_atom`, the scan to the atom's end: 8.6%;
  - `classify_number`: 5.8%;
  - fast_float, the float conversion: 3.9%.
  The tree-building half is the rest: `Parser::run` 12.1%, `read_string`'s own 11.8%,
  `push` 6.6%, the arena, interning.
- **The ceiling.** The proposal removes `classify_number`'s second pass and could put the
  whitespace skip on a class table. If both cost nothing at all, a parse would save 9.6%.
  A real version saves less:
  - integers must still be accumulated, in the scan instead of after it;
  - floats still go to fast_float;
  - a table for whitespace saves part of ten instructions a byte, not all of them.
  The bar cannot be met, so the experiment stopped at the measurement; nothing was written.
- **Real data has fewer numbers, not more.** The reading expected tuning files to be more
  numeric than the benchmark's text. Parsing the stacks' 20 KB of data, scenarios and
  objectives (`baker_parse_corpus`, Release), `classify_number` is 4.4% of the parse,
  against 6.5% on the benchmark's text.
- **What remains true:** a number is read twice, once to find its end and once to classify
  it. If a later profile (a larger, number-heavy load) puts `classify_number` above 10% of
  a parse, the proposal is the right shape, with an agreement test against
  `classify_number`'s grammar over the corpus and the fuzz regressions. The proposal's
  text stays on the `gamepp-reading` branch.

## D203. A field's allowed symbols: `(one_of ...)`

*Requirement note 0015, from the editor integration.*

- **The need.** A symbol field is the data's enumeration: a button, a damage type, a
  behaviour, a material. Its allowed values lived in prose (`desc`) or in the code that
  switches on them. So:
  - a typo passed `check` and failed, or did nothing, in the game;
  - an editor could offer only the values the data already used;
  - an agent had to read the code to learn the choices.
- **Shapes.** A `symbol` or `(list symbol)` field may list what it allows:
  `(button symbol (one_of light heavy kick special))`.
  - **Checking.** `check` reports a value outside the list as an error, as it reports one
    outside a range: `'button' is 'lite', not one of light, heavy, kick, special`, located
    by file, line and path. A list of symbols checks each element. Commands and variables
    are not checked, as before.
  - **Cost.** A list is checked by comparing interned symbols, a pointer compare each. A
    field without a list pays one emptiness test per value.
  - **Declared again.** A field adds the new list's symbols to the old, in the order
    written, as the note asks ("a later layer may add a symbol").
  - **Packs.** Adding a symbol to a harder pack's list loosens it (D145): a warning, and
    the harder list holds.
  - **Mistakes.** A list on a field of another type, or holding something other than
    symbols, is a located declaration error.
  - **`Field::one_of`** is a `List<Symbol>`, kept in the written order, and
    `baker.shapes/1.1` writes it.
- **Tweaks.** `TweakMeta::one_of`, a span of names in code, or `(one_of ...)` after the
  value in data, whose list wins, as the data's range does. `TweakBase::one_of()` reads it,
  `baker.tweaks/1.1` writes it, and the value's metadata strips it, as it strips the other
  keys.
  - **Metadata only, as a tweak's range is.** Nothing checks a tweak's value against its
    range today; checking is the shapes' work. A tweak's list is for panels and editors to
    offer, and a shape's list is what `check` enforces. The editor session is told so.
- **Versions.** Both documents gain the `one_of` field, so both take a new minor version,
  `baker.tweaks/1.1` and `baker.shapes/1.1`, by the documents' rule.
  `docs/reference-json.md` describes the field.
- **Duel** declares its enumerations instead of describing them:
  - `button` lists `light heavy kick special` in place of a `desc` that named them in
    prose;
  - `guard` lists `mid low overhead`.
  The data uses only these, as `fight.cpp` reads them; `(button lite)` in the agent's layer
  now fails Duel's `check` with the error above. All 30 stacks tests pass.
- **Measured** (`tools/bench_compare.py --bench layers_shape --runs 5`, against main):
  every row of the shape-checking benchmark was the same or 2 to 4% faster, three of them
  beyond the noise. Under cachegrind the whole benchmark runs 18,524,674,000 instructions
  against 19,054,221,298 on main. That count is deterministic: main twice differs by 545
  instructions. The fall is the compiler's arrangement of the check's value loop after the
  change; the added work is one test per value. Not traced further.
- **Tests:**
  - shapes: a value and a list element outside their lists; values inside and a command,
    which pass; a redeclaration adding in order; the JSON; the two declaration errors;
    a softer pack adding a symbol;
  - tweaks: the code's list and the data's, with the value read without its metadata;
  - the two JSON documents' expected bytes at 1.1.

## D204. Layers the project file marks for development only

*Requirement note 0009, from the Godot integration.*

- **The need.** Development layers sit beside the shipped data: one person's tuning, an
  agent's proposal awaiting review. A packaging step that takes the data directory whole
  takes them too, and an agent's proposal left on the machine that builds a release then
  ships, unreviewed, above the default layer, and wins. `optional` is not the same thing:
  an optional layer whose file is present loads in every build.
- **`dev_only`** is a layer flag in the project file, beside `optional` and `disabled`, in
  any order after the path: `(agent "data/agent.dta" optional dev_only)`.
  `ProjectLayer::dev_only` carries it, and a Project built in code sets it the same way.
  - **Shipping builds.** `open_project` never pushes such a layer under `BAKER_SHIPPING`,
    whatever its file says. Development builds load it as before.
  - **Packaging.** A packaging step lists the marked layers' files from `read_project`
    alone, no Config: `for (l : project.layers) if (l.dev_only) exclude(l.path)`. The paths
    are as the project names them; a file such a layer only `#include`s is the step's to
    know, which the docs say.
  - **Tools.** `ProjectOptions::skip_dev_only` leaves every marked layer out, as a shipping
    build does: one option instead of a name each.
- **The stacks** mark `local` and `agent` dev_only, in each project file and in
  `base::data_stack`'s built stack for a packaged game.
  - **Commands are unchanged.** They still leave out only `local`, by name: `check` and
    `sim` exist to judge the agent's layer, so they do not use `skip_dev_only`.
  - **One test line.** `stacks/tools/test_commands.py`, which rewrites the project file by
    replacing the agent's line, matches the line's new text.
  - **Packaging, not changed here.** The stacks' CI packaging step still copies `data/*`
    whole into a Release build. That build is not a shipping one, so its marked layers
    still load if their files ship; making that step read the project is a stacks change
    for the owner to call.
- **Docs.** The cookbook's project-file section gives the flag, the packaging pattern and
  `skip_dev_only`, and `project.hpp`'s comment shows the loop.
- **Tests** (`test_project`):
  - the flag is read in either order after the path;
  - the files to leave out are listed from the project alone;
  - in a development build the marked layers load; under shipping they do not, with
    their files present (the shipping matrix runs the same test);
  - `skip_dev_only` leaves them out;
  - the error for an unknown flag names `dev_only`.

## D205. The stacks' packages leave out their development layers

*The owner's call on the follow-up D204 named.*

- **What was wrong.** The CI step that packages each stack for Windows copied `data/*` whole.
  An `agent.dta` or `local.dta` present on the machine that built the package would ship,
  and the packaged game (a Release build, not a shipping one, with the stack built in code
  for want of a project file) would load it above the default layer.
- **`stacks_<game> dev-files`** prints the files of the layers the project marks
  `dev_only`, one a line, as the project names them (`data/local.dta`, `data/agent.dta`).
  It reads the project with Baker's `read_project`, as the game does, so the packaging step
  needs no parser of its own and follows the project file as it changes.
- **The package step** removes those files from the package after copying the data. It
  first plants an agent layer, an agent's proposal left on the build machine, and fails if
  the package still carries it. Simulated locally for Duel: a planted `agent.dta` and
  `local.dta` are left out, and `default.dta` stays.
- The command is in `stacks/agent.md`'s list.

## D206. `check` holds the data to the tweaks the game declares

*Requirement note 0010, from the Godot integration.*

- **The need.** A tweak declares a tunable: its path, its type, often its range or list.
  `check` held data to shapes but not to tweaks, so a game declared each tunable twice or
  its gate passed proposals that could not work. Three edits each passed `check` with exit
  0: `(height "tall")` for a float tweak, `(height 1000)` against a range of 16 to 320, and
  `(hieght 120)` beside `height`. The game then played on a default, or on a value its
  designer ruled out, with nothing on the record.
- **`check_tweaks(root, registry)`** (`<baker/tweak.hpp>`, the data library) holds a merged
  tree to the registered tweaks. It changes no tweak: conversion goes through the tweak's
  own `assign` without applying. It returns `TweakFindings`.
  - **Errors.** A value that does not convert to its tweak's type (located, as the bind
    warning was); a number outside the tweak's range; a symbol outside its `one_of`. The
    data's own `(range ...)` and `(one_of ...)` after the value win over the code's, as they
    do for panels.
  - **Warning: a tweak whose path the data lacks.** The tweak keeps its default; a default
    in code is a choice, so this never fails.
  - **Warning: a key beside a group's tweaks that no tweak reads**, as a misspelled key
    looks: `'hieght' in 'jump' is read by no tweak; its tweaks read air, buffer, height`.
    - **Which groups.** Only groups below the top level, where some tweak reads a sibling,
      so structured data and scenarios stay quiet.
    - **Which keys count as read.** A child that leads to a deeper tweak (`air` for
      `jump.air.time`) is read.
    - **Order.** The names are sorted, so the message does not depend on the order of
      registration.
- **In the report.** `check()` of a Config fills `CheckReport::tweak_errors` and
  `tweak_warnings` from the registry that Config binds (`Config::tweak_registry()`, new).
  Tweak errors fail `ok()`. `baker.check/1.3` adds the two lists, the text form prints them
  as `tweak:` and `tweak warning:` lines, and `docs/check-json.md` describes them. The
  kit's and the stacks' `check` commands get them with no change of their own.
- **Cost.** Nothing new on a tweak's path: the work is in `check`, which resolves each
  tweak's path once and lists each group's children once.
- **The stacks.** Their committed data passes their own tweaks. Stuntline has three
  warnings: `chase_distance`, `chase_height` and `patience` have no value in the data and
  run on their code defaults. That is the finding the warning exists for, and the stack's
  to settle.
- **Tests:**
  - in `test_tweaks_json`: clean data, top-level structured data quiet, the note's three
    edits (a conversion error, a range error located at its line, a misspelled key), a
    missing tweak, the data's own range and list winning, and a list violation;
  - in `test_check`: `check()` of a Config filling both lists, failing on the error, the
    JSON's new list, and a Config without a registry staying quiet;
  - the expected JSON documents moved to 1.3.

## D207. Plan A, the agent bridge, is closed

*The owner's decision.*

- **What happened.** The owner archived `baker-mcp` and its session. The MCP server showed
  no benefit over what an agent already has: the game binary's commands and the
  stacks-agent skill.
- **Why that settles it.** This is the comparison the plan itself made its kill criterion
  (`dev/integration-plans.md`, Plan A, A3): the server had to earn its place against the
  CLI and the skill.
- **What stays in Baker.** Everything the plan asked of Baker is used by the commands, the
  editor integration and the stacks:
  - `eval --json` and `baker.reply`;
  - check's report and its gate;
  - the reference JSON;
  - `promote --patch`;
  - the host kit's `run_command`.
  Nothing in Baker depended on the server, so nothing is removed.
- **The record.** Plan A's section and the summary table are marked closed. The
  remaining integrations are the editor (B), Godot (C) and published overlays (D).

## D208. Scenarios a tool can find and run: the kit's `sim` and `compare`

*Requirement note 0016 (`dev/scenario-runner-note.md`); the owner's answers: the host kit,
the stacks move onto it, `compare` included.*

- **The need.** A tool (an editor's test view, CI, an agent) should find a game's scenarios
  and objectives, run one by name with a seed, and read a located, versioned result,
  without knowing the game. Until now only the stacks could, in their own format.
- **The project file** gains three optional entries, read by `read_project`, ignored by
  `open_project`, each relative to the project file: `(tool PATH)`, the binary that answers
  the commands (`Project::tool`, `tool_at`); `(scenarios PATH...)` and
  `(objectives PATH...)`, files or directories (`Project::scenarios`, `objectives`).
- **The host kit's runner** (`src/host/scenarios.{hpp,cpp}`, internal):
  - the stacks' `(scenario ...)` and `(objective ...)` forms, each kept with its file and
    line, and a malformed form an error at its line;
  - a run reset with the seed (`$seed`), its start commands, fixed steps through the new
    `CommandHooks::step` with the scheduler ticked and each `(at ...)` run on time, then
    each objective's measure and pass. Steps are counted from integer nanoseconds per
    step, so a scenario takes the same steps everywhere;
  - `(over median)` objectives judged once per scenario on the median over the seeds.
- **Commands.** `sim [--scenario NAME]... [--seed N | --seeds A-B] [--step-hz N] [--json]
  [--results DIR]` and `compare --before DIR --after DIR`. `--scenario` takes a scenario's
  name or its file's stem. `--results` writes `results.json` and `results.dta`; `compare`
  reads the `.dta` back, so the kit needs no JSON reader. Exit codes 0, 1, 2 as the other
  commands.
- **JSON.** `baker.sim/1` and `baker.compare/1` (`docs/sim-json.md`). Every run, objective,
  median and compared objective carries `file` and `line`; a failed command or measure is a
  problem in check's shape (code, file, line, column, path, message).
- **`check`** reads the project's scenarios and objectives as extra trees, so their
  commands are checked with the data (an agent writes them as it writes data). The test
  game's own flag moved from `--scenario` to `--extra`, since the kit now owns the name.
- **Size.** The runner is about 790 lines, not the note's 450: located errors, compare's
  median verdicts and the results data it reads back. After trimming (a JSON error and
  bool helper, one way to note a change in compare, a table of text options in the
  parser), `baker::host` is 2,106 lines. The owner raised its budget to 2,200
  (`tools/count_lines.py`).
- **Tests.** `tests/host/test_scenarios.cpp` ports the stacks' runner tests onto a counter
  game: steps and judging, start and `at` times, repeatability, located errors, a measure
  that gives no number, a path that does not exist, results that read back located, and
  medians with compare. `tests/host/test_commands.py` runs `sim` and `compare` through the
  test game, whose stack gains a scenario and two objectives.
- **The stacks** move onto it, as they moved onto the kit's other commands (D192):
  - their project files name `(scenarios "sim")` and `(objectives "metrics")`, and the
    stack built in code for a packaged game names the same directories beside its data;
  - `base::Game::step` is the kit's `step`; `stacks/base`'s own runner (`sim.cpp`,
    `sim.hpp`, `test_sim.cpp`, about 800 lines with its tests) is gone, with
    `stacks.sim/1.1`, `stacks.compare/1.1`, and the `--sim`, `--metrics` and `--out`
    flags. `compare` takes `--before` and `--after`;
  - they name no `(tool ...)`: the binary's path is the build directory's, which a project
    file in the source tree cannot know. A tool that builds the stack knows it;
  - every stack's `sim` and `check` tests pass on their own data, and `test_commands.py`
    checks the new schemas and that runs and broken objectives are located.

## D209. A scenario run the host advances from its own loop: `SimRun`

*Requirement note 0011, from the Godot integration (`dev/stepped-sim-note.md`).*

- **The need.** `sim` stepped a game through `CommandHooks::step`, in a loop inside one
  call. An engine that owns its main loop advances its physics only inside that loop, a
  frame at a time, with no call that runs one step and comes back. A game in it could not
  write `step` faithfully: stepping its systems from inside one call skipped the engine's
  physics, and the runs were wrong and not repeatable. Such a game kept its own runner and
  its own result format.
- **`baker::host::SimRun`** (`<baker/host/commands.hpp>`) is a `sim` the host advances:
  `wants()` says `Reset`, `Steps` or `Done`; the host resets its world with `seed()` over as
  many frames as it takes and calls `ready()`; then it calls `step()` once per fixed step of
  `step_seconds()`; at the end `status()` is the exit status and the output has gone where
  `run_command` sends it. `stop()` ends early.
- **One runner, two drivers.** The runner became `sim::Stepper` (`begin`, `advance`,
  `judge`, `stop`); `simulate` is a loop over it. `sim` through `run_command` is a `SimRun`
  driven with the hooks' `reset` and `step`, so both paths give the same runs, bytes and exit
  status; a test checks the bytes, with a reset that takes three frames.
- **Reset.** `SimRun` sets `$seed` and waits; it never calls `reset` or `step` itself.
  Waiting counts no steps.
- **Stopping.** The run in progress is kept with its steps, not finished and not judged;
  runs not started are left out; status 2. `baker.sim/1.1` adds `finished` to each run and
  `"stopped"` as a status; `results.dta` writes `(finished 0)` and `read_results` reads it.
- **Cost.** A step with nothing due is a scheduler tick, a count and a compare; a test holds
  it to no allocation. The kit is 2,299 lines; the owner raised its budget to 2,400.
- **Output.** `run_command` and `SimRun` share one `send` for `--output`, the host's sink or
  stdout. A `SimRun` call gathers what it prints while it runs, so calls spread over frames
  write one output.

## D210. A tool registers tweaks from their descriptions: `DescribedTweaks`

*Requirement note 0017, from the editor integration.*

- **The need.** Since D206, `check` holds data to the game's tweaks. A tool that checks data
  without the game (a language server, a CI checker, an agent's pre-flight check) has the
  reference but not the code. It could make a `Tweak<T>` for each of Baker's types by
  switching on the type name, but not for a type of the game's own: `(aim 1)` where a `vec2`
  is wanted passed in the editor and failed in the game's `check`.
- **Value types.** A converter may declare `values`, Baker's type of each value it reads:
  `static constexpr std::string_view values[] = {"float32", "float32"}`. `TweakBase` gains
  `value_types()`, from that, or the type itself for Baker's single-value types, else empty.
  `baker.tweaks/1.2` writes it as `values` (null when empty). A type the game does not
  describe keeps working as before.
- **`DescribedTweaks`** (`<baker/tweak.hpp>`) owns tweaks made from `TweakDescription`s
  (path, type, values, default as Baker text, range, step, desc, kind, group, dev_only,
  one_of) and registers them with a registry. `add` is one call for every type. A described
  tweak reads each value with `Values::read` into Baker's type, as a game's converter does,
  so its findings are the game's: a test runs the note's edits against a game's registry and
  a described copy and compares every message.
- **Errors.** A type that is not Baker's with no `values`, a value type that is not
  Baker's, or a default that does not read as its types is an error naming the tweak; the
  tweak is not added.
- **Not covered.** A list's count of values is the data's, so a list tweak has no
  `values` and cannot be described yet.
- **Cost.** Nothing on a tweak's read path: `value_types()` is a virtual used by the JSON
  and tools. A game that does not use `DescribedTweaks` gains one vtable entry per tweak
  type. The core is 14,492 lines of its 15,000.

## D211. A tool declares the game's types, objects and value types from the reference

*Requirement note 0018, from the editor integration (`dev/declared-types-note.md`).*

- **The need.** A tool declares the game's functions from the reference (D175), but not its
  types or named objects, so `{scheduler delay_task 2 {...}}` was not checked at all; and a
  parameter of a game type (`vec2`) could not be declared, or took any value.
- **Value types of a parameter.** A parameter's `values` come from `detail::value_types<T>()`
  (D210, moved to `array.hpp`), an optional's from its value's. `baker.reference/1.2` writes
  them on each parameter, `null` when the type does not say. `ParamDescription::values`
  lets `declare` take a type it does not know; `check` holds a literal argument to the types
  through `detail::reads_as`, reading an array as its contents as `Values::read` reads a
  compound value. A declared parameter of one of Baker's types gets that type as its value
  types, as the native has, so a declared function's reference entry stays the game's.
- **Types and objects.** `Context::declare_type(name, bases, methods)` builds a type record
  from `MethodDescription`s (each method's overloads built as `declare` builds a function's,
  through one `build_described`), inherits its bases, which must be known, and registers it;
  the Context owns and frees it. `Context::declare_object(name, type)` keeps a name and a
  type, which `check` reads after live objects. A call on either fails at run time.
- **The call path.** Putting `values` beside `match` grew `ParamInfo` from 32 to 48 bytes,
  and the promoted overload row of `bench_call` slowed by 8.9% (47.8 to 52.0 ns, nine runs
  a side, ranges apart), twice. So a parameter's type name and value types moved behind a
  pointer, `ParamInfo::text` (a `ParamText`, static for natives), read through `type()`
  and `values()`; `ParamInfo` is 32 bytes again. Against 3270a23, nine runs a side:
  `{+ 1.5 2}` 47.9 to 46.8 ns, `{add 1 2}` 32.0 to 32.4, `{bob grow 0}` 62.0 to 61.8, all
  within their ranges. `handle_all` moved 427 to 375 ns (ranges apart) and the no-Context
  `handle` 8.6 to 7.5; the message path reads no parameter table, and `count_instructions`
  has no message case to settle it, so it is recorded, not claimed. The parse, lookup,
  set and frame instruction counts are unchanged.
- **Tests.** A game with a type, a derived type, a named object and a `vec2` function, and a
  tool declaring the same from descriptions, check one tree to identical errors and demand;
  the refusals name the type, method, base or object.

## D212. Fixed-size lists describe their values; the console's epoch and port

*Requirement notes 0019 and 0020, from the editor integration.*

- **0019: fixed-size lists.** `Convert<std::array<T, N>>` declares `values`: N of T's value
  type when T is one of Baker's single-value types (`std::array<float, 3>` is three
  `float32`s), at compile time. A T of the game's own leaves `values` empty: its values
  nest in each element, `((0 1) (2 3))`, and a flat list would describe the wrong layout.
  The reference writes them (`baker.tweaks/1.2`, `baker.reference/1.2`, no new field), so a
  tool registers such a tweak with `DescribedTweaks` and declares such a parameter. A test
  checks the note's edits, `(color 0.9 0.5)` and `(color red 0.5 0.2)`, against a game's
  tweak and a described copy, errors and warnings alike. A difference remains: the
  converter takes exactly N values and a described tweak at least N, so a fourth value
  passes the tool and fails the game. `std::vector` still declares none.
- **0020: `{config.epoch}`.** A config tool answering `Config::epoch()`: a tool polls it
  and asks for values again only when it moved. Nothing is kept that was not kept before.
- **0020: `(console PORT)`.** `read_project` reads it into `Project::console_port` (0 when
  absent) with its position; a port outside 1 to 65535, or not a number, is an error at its
  line. A game may use it as its default; Baker does not open a console from it.
- **The stacks** name a port each, Cube 4555 to Wayfarer 4561, and the base uses it unless
  `--console-port` is given, so the seven run at once. `stacks/tools/console.py --stack DIR`
  reads the port from a stack's project file. A packaged game has no project file and keeps
  4555.

## D213. A function with a body and a described signature

*Requirement note 0012 of the Godot integration (`dev/godot-notes-2-note.md`).*

- **The need.** A host with a scripting language of its own binds that language's functions
  at runtime, with no C++ signature to deduce; taking `Args` reads as "values...", so
  `check` passed a call with any arguments.
- **`Context::declare(name, overloads, body)`** is `declare` with a body: the description is
  checked as `declare` checks it, `check` and the reference see it, and a call runs `body`
  (`Callback<Expected<Node>(Args&)>`, kept in the declared function's own callable, so
  nothing is added to a native's path).
- **At a call.** Arity is the overload's, as for any function. Without a raw parameter, each
  argument is evaluated once and held to its parameter, by its match or by the value types
  of a type only described; a mismatch is the native's message ("argument 1 of 'f' is
  symbol, not int") and the body does not run; the body gets the evaluated `Args`. With a raw
  parameter, the body gets the arguments as written. The evaluated arguments sit in eight
  stack slots and spill to the default allocator, as the call path's do, not in a `Vec`.

## D214. Scenario directories listed through the host

*Requirement note 0013 of the Godot integration.*

- **The need.** The kit read a single scenarios file through the host's reader, but listed a
  directory with the disk's calls, so in a game whose files are in an archive a directory
  read nothing, and the shipped build's `sim` ran nothing.
- **`CommandHooks::list`** lists a directory of the host's own files; the kit keeps the
  `.dta` names, sorts them, and reads each through `reader`. Without it, or when it says
  `dir` is not one of its directories, the disk's directory is read as before. An entry can
  already name several files.
- **Tests.** A book read through a lister serving files in any order reads them in name
  order; without the lister the same directory reads nothing; the test game lists its
  `game://` directories, and `sim --root` runs the stack's scenario directory through them.

## D215. Allowed values for string and integer tweaks

*Requirement note 0014 of the Godot integration.*

- **The need.** Engines offer a choice among names in two forms, a string from a list or an
  integer with named values. Only a symbol tweak carried a list, so a misspelled
  `(attack "hevy")` passed `check`.
- **Strings** take `TweakMeta::one_of` as symbols do, or `(one_of "light" "heavy")` after the
  value. **Integers** take `TweakMeta::choices` (`TweakChoice{value, name}`), or
  `(one_of (0 easy) (1 normal))`, a bare number being a choice without a name. The data's
  list wins over the code's.
- **`check_tweaks`** holds a symbol or a string to the texts and an integer to the choices,
  located, naming the choices with their names; the tweak still takes any value.
- **`baker.tweaks/1.3`.** `one_of` also lists a string tweak's strings; `choices` lists an
  integer tweak's `{"value", "name"}`s. `TweakBase::choices()` gives them in code, with
  names interned. `TweakDescription` takes `choices`, so a tool's described tweak checks
  alike (a test compares them).

## D216. The live loop's console on a free port

*Requirement note 0015 of the Godot integration.*

`LiveOptions::console_any_port` listens on a free port (`Console::listen(0)`), which
`console_port()` reports, for a tool that starts several instances. A `console_port` of 0
keeps meaning no console, and a shipping build opens none. A test opens two loops and gets
two ports.

## D217. The project's tool with arguments

*Requirement note 0016 of the Godot integration.*

`(tool "path" "argument"...)`: `Project::tool` is the binary, joined with the project
file's directory as before, and `Project::tool_args` the arguments as written, interned, in
order. A tool runs them from the project file's directory, so a relative path among them
names what it names there; Baker cannot tell which arguments are paths, so it joins none.
An argument that is not a string is an error at its line.

## D218. Baker's metadata and color rules, callable on any value

*Requirement note 0022, from the editor integration.*

- **The need.** A tool presenting data decides for every value whether its trailing arrays
  are metadata, and whether it is a color. Baker decided both only for registered tweaks, so
  a tool repeated the rules from the documentation and could differ.
- **`split_metadata(values)`** (`<baker/tweak.hpp>`) gives the value and the trailing
  metadata arrays as a tweak reads them (range, step, desc, note, kind, group, one_of, from
  the end); `(ball (kind ball) (pos 1 2))` is all value. It returns views and allocates
  nothing.
- **`presented_kind(values, kind, range)`**: the data's `(kind ...)`, else `kind`; else
  "color" for 3 or 4 numbers ranged 0..1 by the data's `(range ...)` else `range`.
  `TweakBase::kind()` now calls the same rule (`kind_of`), so they cannot disagree; a test
  compares a tweak's kind with the function's on the same data.

## D219. Errors a tool tells apart without reading their words

*Requirement note 0023, from the editor integration.*

- **The need.** A tool matched words: "already" in `declare_type`'s refusal, quoted names in
  `check_tweaks`'s findings.
- **Codes.** `declare_type` and `declare_object` refuse a name already taken with
  `ErrorCode::Refused`; everything else they refuse stays `ErrorCode::Type`.
  `Context::has_type(name)` answers before asking.
- **Paths.** Every `check_tweaks` finding carries its subject as its path: the tweak's, or
  for a stray key the key's own, group then key. The messages are unchanged.

## D220. A tolerant read of text, for tools; the core's budget is 15,500

*Requirement note 0021, from the editor integration. The owner raised the core's soft
budget from 15,000 to 15,500 for this note and 0024.*

- **The need.** An editor needs the structure of text that rarely parses: `(kestrel (hea`.
  Each tool wrote its own lexer, a second copy of Baker's syntax that no test held to it.
- **`read_structure(text, file)`** (`<baker/parse.hpp>`) runs the parser's own `Lexer` in a
  tolerant mode (`tolerate`): each problem goes to a list and the lexing goes on; an
  unterminated `/*` comment, string or quoted symbol runs to the end; an out-of-range
  number and a lone `$` are tokens still. Comments are reported by span. On top, a
  bracket walk records each bracket's parent, its tag (its first token when that is a
  symbol), and whether it closed; an unclosed one runs to the end, a stray close is left
  out, and a close of the wrong kind closes the innermost. A symbol this text defines as a
  macro at the top level, `[NAME ...]`, is a `Macro` token where it is used. Nothing is
  expanded and no include followed. It returns `List`s of tokens, brackets, comments and
  errors, located as the parser's.
- **The parser's path.** The lexer gains two pointers, tested only on a comment and on the
  error paths. The parse instruction count went from 1,849,845 to 1,845,499 per document
  (-0.23%, `count_instructions.py` against 47ee1fa); lookup is unchanged.
- **Cost of a read.** 2,097,260 bytes, 549,454 tokens: 25.2 ms (best of seven, Release, this
  machine); the editor's own lexer took about 30 ms for 2 MB.
- **Tests.** The tokens, brackets and comments of text that parses; text being typed; an
  unterminated string and comment; stray and mismatched closes; out-of-range numbers; a
  macro and its uses; every corpus file that parses reads with no errors and every bracket
  closed.

## D221. A check after an edit reuses the layers whose text did not change: `reload()`

*Requirement note 0024, from the editor integration; no new API, the owner's choice.*

- **The need.** An editor checks the stack on every pause; on a 2 MB `default.dta` a check
  read the whole stack each time, though the person typed in a ten-line `local.dta`.
- **Already there.** A Config records, for each layer, the content hash of every file it
  read through the reader (`Dep`). `reload()` reads each layer's files through the reader,
  compares the hashes, and reads again only from the first layer whose text changed,
  upward, since macros flow up. So a tool that keeps one Config and calls `reload()` after
  each edit gets reuse keyed by text, with no file times; the Config is the cache, and the
  tool bounds it by keeping it or not.
- **Numbers.** A 2 MB `default.dta` with a one-line `local.dta`, read through an in-memory
  reader: a fresh load 57.2 ms, a `reload()` after an edit to `local.dta` 2.0 ms (best of
  five, Release); `default`'s tree is the same object after it.
- **A cache across fresh Configs** was weighed and not built: it would have to fingerprint
  the macros and defines each layer reads from the layers below, for the case of a tool that
  cannot keep a Config, and none needs that yet.
- **Tests.** After an edit to the top layer, the kept Config's merged tree and sources are a
  fresh load's and the bottom layer was not read again; text that does not parse gives the
  fresh load's error, and the next edit reads.
