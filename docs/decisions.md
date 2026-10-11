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
  `moira_parse_corpus --roundtrip <checkout>`; **not yet run.** Record the result here.
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
