# Recipes: how to check each rule

These are the commands behind the checks in SKILL.md's review procedure, for a C++ library
built with GCC or Clang (and MSVC where noted). Each recipe carries its result on one real
engine-core library, for calibration.

**The calibration library** is Baker, an MIT C++20 data language and embedded scripting
system, at commit `77124a6` (2026-10-30). It was built with GCC 13.2 and Clang 18.1 on
x86-64, in a 4-core KVM guest.

Re-run a recipe before quoting its number in a finding: these numbers are a baseline for
judging scale, not evidence about another codebase.

**Flags for a hand-run compile.** Take them from `compile_commands.json`
(`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`), so that defines, include paths and
`-fno-exceptions` match the real build.

## Layout

**Padding, all of it:**
```sh
clang++ <flags> -fsyntax-only -Wpadded file.cpp
```
It warns for every hole. Most are harmless: a 16-byte tagged value with 7 bytes of padding
can be the design. Read only the structs a hot loop touches, and those iterated in arrays.

**One struct's offsets:**
```sh
clang++ <flags> -fsyntax-only -Xclang -fdump-record-layouts file.cpp > layouts.txt
```
Then search the output for `| struct ns::Name$`. The equivalents elsewhere are MSVC's
undocumented, long-standing `/d1reportSingleClassLayoutName` and `pahole` on a debug
object (`pahole --reorganize` proposes an order). Ask:
- which cache line each hot field is on;
- whether a field the hot path reads sits past byte 64.

*Baker:* the function record's call fields were at bytes 8–40 and its counters at 40–64.
Two cold fields at 64 were not read on the call path.

**The size and offset of a value a game reads.** Compile a one-line reader and read the
assembly:
```cpp
float read(const Tweak<float>& t) { return t; }
```
*Baker:* `movss 180(%rdi), %xmm0`. That is one load, but it shows the object is 184 bytes,
with 112 bytes of tool metadata before the value, in shipping as well.

## Allocation

A replaced global `operator new`, counting, sees every allocation that reaches the global
heap. It cannot see a case no test runs, so every fixed inline capacity needs a test one
past it. To list the capacities:
```sh
grep -rnE '\[[0-9]+\];|kInline|kSmall' src include
```
To count from outside a test, install a counting allocator through the library's
allocator hook, before anything allocates.

*Baker:* an overload call with 8 arguments made 0 allocations in 1,000 calls. With 9 it
made 1,000, from the process default allocator rather than the owner's. No test passed 9.

**Allocations that buy nothing:** `valgrind --tool=dhat ./bench`, then open the output in
`dh_view.html`. Filter by "short-lived" and by "zero reads or zero writes".

## Code size

**Per instantiation.** Generate a translation unit that binds N callables of a few
signatures. Compile it at two sizes, and divide the difference in `size` by the difference
in N:
```sh
size b161.o b41.o
nm -C --size-sort -S b41.o   # then group symbols with the lambda's number stripped
```
*Baker,* binding with `ctx.func` (GCC 13):

| Build | Code per binding | Compile time per binding |
|---|---|---|
| `-O2` | about 850 bytes, 320 of them registration code inlined at the call site | about 22 ms |
| `-O0` | about 2.1 KB | about 11 ms |

Registration runs once, so it belongs out of line. A thunk that a frame executes competes
with the hot loop for the instruction cache. Luau keeps its interpreter's core near 16 KB,
about half of L1i (`sources.md`).

**Whole library:** `size -t build/lib*.a`. What a game links is smaller, so measure a
minimal game linked with `--gc-sections` (`/OPT:REF` on MSVC), not the archive.

**Diffs over time:**
- Bloaty for ELF and Mach-O: `bloaty -d compileunits,symbols new -- old`.
- SizeBench for PE files with a PDB. It also flags "Wasteful Virtuals" and "Template
  Foldability".

## Compile time

**What a header costs a translation unit:**
```sh
printf '#include <lib/header.hpp>\nint main(){}\n' > t.cpp
clang++ -std=c++20 -O2 -Iinclude -E t.cpp | wc -l        # preprocessed lines
clang++ -std=c++20 -O2 -Iinclude -ftime-trace -c t.cpp   # t.json: chrome://tracing or Perfetto
```
Across a whole build, ClangBuildAnalyzer aggregates `-ftime-trace` output (slowest parses,
instantiations, most-included headers), and `vcperf` does the same from MSVC's Build
Insights.

*Baker* (Clang 18, C++20, `-O2`, a full compile of the one-line file):
- An empty translation unit took 0.22 s.
- One public header took 0.39–0.56 s and 34,000–37,300 preprocessed lines.
- The costliest header was about as costly as `<vector>`, `<string>` and `<unordered_map>`
  together (43,300 lines).

Most of the cost was standard headers. In C++20, `<string_view>` alone is 18,600 lines
(12,400 in C++17), `<span>` 16,200 and `<optional>` 9,800.

So about 0.2 s per including file is the floor for any public header that uses them. A
game with 2,000 including files pays about 400 s of CPU per clean build.

## Startup and static initialization

```sh
nm -A build/lib*.a | grep _GLOBAL__sub_I                 # objects with a dynamic initializer
objdump -dr -C x.o | awk '/<_GLOBAL__sub_I/{p=1} p'      # what it constructs
clang++ <flags> -fsyntax-only -Wglobal-constructors -Wexit-time-destructors file.cpp
```
The test for a namespace-scope object is `constinit`: if it compiles, there is no
initializer to order. Chromium gates its count of static initializers in CI
(`sources.md`).

*Baker:* nine translation units ran one, all for the same `inline const` value. Its
initializer was not `constexpr`, so each unit paid a guard and an `atexit` registration. A
function-local `static const` of the same type added a guard check to every call of its
function.

## What a shipping binary carries

```sh
g++ <flags> -DSHIPPING=1 -c t.cpp && strings t.o | grep <marker>
nm -C lib.a | grep -c 'typeinfo for'
```
Put a recognizable marker in every tool-only string (a description, a group, a doc), and
look for it. Look also for file paths, parameter names, and type names (`typeinfo name`).

*Baker:* a tuned value declared with a description and a group kept both strings in the
shipping object. Typeinfo was emitted because the build never passed `-fno-rtti`, although
nothing used RTTI. With `-fno-rtti` the library built cleanly and passed all 23 test
suites.

## Debug builds

Build the same benchmarks with `-DCMAKE_BUILD_TYPE=Debug`, and with `-Og`, and report each
row's ratio to release.

*Baker* (GCC 13):

| Row | Release | `-Og` | `-O0` | `-O0` ratio |
|---|---|---|---|---|
| a native call, 2 int arguments | 29 ns | 73 ns | 332 ns | 11× |
| a typed message | 7.7 ns | 22 ns | 94 ns | 12× |
| a variable read | 3.7 ns | 13 ns | 50 ns | 14× |
| a broadcast to 64 objects | 382 ns | 967 ns | 3,306 ns | 8.6× |
| a call, cache-cold | 155 ns | 262 ns | 575 ns | 3.7× |
| a 20-element linear lookup | 11 ns | | 84 ns | 7.4× |
| a 20-element sorted lookup | 17 ns | | 291 ns | 17× |
| 1,000 elements, linear and sorted | 177 and 74 ns | | 479 and 520 ns | |

Cold calls lose least, because a miss costs the same at every optimization level. The
sorted lookup went through `std::partition_point` with a lambda. It had the worst ratio,
and at `-O0` it lost to the linear scan even at 1,000 elements, where in release it won by
2.4×. `-Og` keeps most of the speed.

**MSVC's debug build** differs from GCC's (from Microsoft's documentation):
- `__forceinline` is not honored under `/Ob0`, the debug default. GCC's `always_inline`
  inlines even at `-O0`.
- Since VS 2022 17.5, `std::move` and `std::forward` are casts, not calls
  (`[[msvc::intrinsic]]`, under `/permissive-`).
- `/JMC` puts a call in every function's prologue, `/RTC1` adds stack checks, and
  `_ITERATOR_DEBUG_LEVEL=2` checks every iterator.
- `#pragma detect_mismatch` refuses to link a library built with another
  `_ITERATOR_DEBUG_LEVEL` or runtime library (LNK2038).

Engines commonly build core libraries optimized inside a debug game (Unreal's DebugGame
configuration, for one). For an MSVC library that means building with the game's debug
runtime library and iterator level, setting `/O2` on the library's targets, and removing
`/RTC1`. MSVC's `/dynamicdeopt` (VS 2022 17.14) debugs optimized code instead. It is
incompatible with `/GL`, LTCG, `/RTC` and ASan.

## Costs that a best-of-N row hides

A best-of-N benchmark is right for a regression and is the opposite of a frame's worst
case. For code that runs inside a frame, also measure:
- **The first call in a fresh process.** Time the 1st, 2nd and 100th call in one run. A
  jump between the first and second shows lazy initialization.

  *Baker:* 0.65–0.84 µs for the first call, then 0.07 µs. Constructing a scripting
  context took 56–116 µs.
- **The largest single cost:** a reload with many changes, a large load, waking 10,000
  tasks at once. Record the maximum, not the minimum, then convert it to a frame: 1 ms is
  6% of a 16.6 ms frame.
- **The distribution** over at least 100 runs: the 99th percentile and the maximum.
  Android's frame-rate guidance treats P99 as the stutter indicator.
- **Page faults:** `getrusage(RUSAGE_SELF)`'s `ru_minflt` before and after the work
  (`PageFaultCount` from `GetProcessMemoryInfo` on Windows). Fresh memory faults every
  4 KB on first touch, and glibc returns large blocks to the system.

## Codegen

Read the instructions for any claim about what the compiler does with a hot line:
```sh
g++ <flags> -O3 -S -o - t.cpp      # or clang++; Compiler Explorer for MSVC and AArch64
```
- **An atomic in a loop:** `for (i) v[i] *= speed;`, where `speed` is a relaxed atomic
  float, gives 0 `mulps` on GCC 13 and Clang 18. With `const float s = speed;` before the
  loop, both give 2. The compiler never hoists an atomic load, so the loop never
  vectorizes.
- **Contraction:** count `vfmadd`/`vfmsub` in a numeric file built for
  `-march=x86-64-v3`, then again with `-ffp-contract=off`. *Baker:* its curve evaluation
  had 10 with GCC 13, 8 with Clang 18, and 0 with contraction off.
- **Ghost reads:** in a loop over a member array, look for the member's reload on every
  iteration. Then hoist it into a local, as the Code Clinic says.

## Instructions and cache misses

Under `valgrind`:
- **Instruction counts on two commits:** cachegrind. Counts are stable enough to compare
  where timings are noise.
- **Where the instructions go:** `valgrind --tool=callgrind ./bench`, then
  `callgrind_annotate`.

  *Baker,* parsing 4.2 MB: the lexer 28%, the parser loop 19%, number classification 10%,
  node push 10%, interning 8%.
- **Branch mispredictions and real cache misses:** `perf stat -e
  branch-misses,cache-misses` where `perf` exists. Measure them on mixed real data, not on
  one call repeated, which the predictor learns.

## Information density

The Code Clinic measures how much a field is worth reading:
1. Write the field's value every frame (or every call) for 10,000 frames.
2. Compress the dump with `gzip -9` or `xz`.
3. Divide the compressed bits by the frame count. Compare that with the cache-line bytes
   loaded to read the field.

His example compressed to 0.732 bits a frame, against 128 bytes loaded. A field that
compresses to almost nothing is a state to sort by, not a value to test.

## Threads

- **TSan and fences:** `g++ -fsanitize=thread` on code using `std::atomic_thread_fence`
  warns "'atomic_thread_fence' is not supported with '-fsanitize=thread'", and Clang's
  TSan is silent about the same gap. A clean TSan run says nothing about a protocol built
  on fences, such as a seqlock.
- **Weakly ordered hardware:** GitHub's `ubuntu-24.04-arm` and `windows-11-arm` runners
  exist. A stress test, one writer and N readers checking an invariant, belongs there.
  Without an AArch64 sysroot, `clang --target=aarch64-linux-gnu -S` cannot compile code
  that includes standard headers; use Compiler Explorer to read the code.
- **Fibers:** list the `thread_local`s (`grep -rn thread_local src include`). For each one,
  say what happens if a fiber switches, or moves to another thread, inside a callback.

## Locale

`printf("%g")`, `strtod` and `localeconv` follow `LC_NUMERIC`. A host that calls
`setlocale(LC_ALL, "")` gets the user's locale for all of them, and Qt applications do this
by default on Unix. A container often has only `C` and `POSIX`, so a test that skips when
its locale is absent proves nothing there. Make one:
```sh
localedef -i de_DE -f UTF-8 $SCRATCH/loc/de_DE.UTF-8 && LOCPATH=$SCRATCH/loc ./test
```
Under it, `snprintf("%.6g", 1.5)` prints `1,5`.

*Baker:* its data formatter corrects for this, and one host-side JSON writer did not.

## Tools a TD will use

| Tool | What it shows | Notes |
|---|---|---|
| Tracy | Zones at about 2.25 ns each, per-frame plots, named memory pools (`TracyAllocN`) | A zone's source location must outlive the program, so names from a reloadable script need transient zones or an immortal string table |
| Superluminal, PIX | Sampling and event timelines on Windows and Xbox | Superluminal samples at 8 kHz, so a 30 ns call is invisible without instrumentation |
| ClangBuildAnalyzer, `vcperf` | The slowest headers and instantiations across a build | |
| Bloaty, SizeBench | Size diffs by compile unit and symbol | |
| `pahole`, `/d1reportSingleClassLayout` | Struct holes and cache-line boundaries | |
| `perf`, VTune | Hardware counters | |

When one of these is not available, a finding names it as the next step, rather than
estimating what it would show.
