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
