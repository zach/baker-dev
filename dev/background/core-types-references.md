# Core value types: references from mainstream engines

Evidence gathered in October 2026 to check Baker's core-type decisions (Node layout,
numeric width, symbols, strings, arrays, refcounting, object handles, error handling,
toolchains) against commercial PC, console and mobile C++ practice. It led to decisions
D18–D23 in `dev/decisions.md`.

Two passes produced it:

1. **A verified research pass** (in this session): five search angles, 25 sources, and 25
   claims, each checked by three independent votes, with no claims refuted. It could
   reach Lua, Luau, Godot, EnTT, flecs, LLVM and the Android NDK documentation, but not
   Epic's documentation or any studio material.
2. **A report from an agent with wider web access**, imported below unedited. It fills
   the Unreal and studio gaps, but it was **not** adversarially verified, and spot checks
   found errors and quotes that could not be traced to their sources. Read it through the
   verification notes.

---

## Verification notes on the imported report

Checked on 2026-10-11 against primary sources.

**Confirmed**

- `EASTL_EXCEPTIONS_ENABLED` defaults to 0: EASTL's `include/EASTL/internal/config.h`
  says "we do not enable EASTL exceptions by default if the compiler has exceptions
  enabled".
- libc++ gates double `std::from_chars` on Apple behind `_LIBCPP_INTRODUCED_IN_LLVM_20`,
  which on `main` maps to macOS/iOS deployment targets of **26.0** or later
  (`libcxx/include/__configuration/availability.h`). In the `release/20.x` header the
  Apple mapping is still a placeholder, which makes it unavailable on every Apple target.
- `std::expected` arrived in libstdc++ **12** and libc++ **16** (cppreference's C++23
  compiler-support table, mirrored).
- The Naughty Dog GDC 2015 talk exists and covers a fiber job system, memory allocation
  patterns and lock strategies (GDC Vault session description).

**Wrong**

- The report puts double `from_chars` on Apple at "macOS 15.0+ / iOS 18.0+". It is 26.0
  (see above).
- The report puts `std::expected` at "GCC 13.1+". It is GCC 12.
- `EASTL_FORCE_INTERLOCKED` does not appear in EASTL's configuration header.

**Unverified; treat with caution**

- **The quoted passage attributed to Christian Gyrling** ("Data passed to worker jobs is
  stored in transient linear arenas ... read by worker threads without refcounts or
  atomic locks") could not be found. It reads as a paraphrase presented as a quote. The
  report's conclusion that non-atomic refcounting is "superior" depends on it, so D20
  does not rely on it.
- The `PLATFORM_EXCEPTIONS_DISABLED` "for all shipping targets" quote. Unreal's source
  is not public, and the quote could not be checked.
- **NDK r29 = LLVM 20.0.0.** The r29 changelog names `clang-r563880c` but gives no LLVM
  version.
- Generalizations with no source: hex-literal practice, "std::function / std::filesystem
  are disallowed", "open-world titles routinely exceed 65,536 entities", and the measured
  atomic-cost range.
- One secondary source is a game-cheating forum (UnKnoWnCheaTs). It is not used for any
  decision.

**Consistent with the maintainer's own knowledge, not independently re-checked here:**
`FName`'s index layout and case-insensitive, case-preserving comparison; `FName` entries
never being freed; `FWeakObjectPtr`'s 32-bit `ObjectIndex` plus 32-bit
`ObjectSerialNumber`; UE5 defaulting `TSharedPtr` to `ESPMode::ThreadSafe`; UE 5.3
switching to C++20; UE5 Large World Coordinates moving to double; `TValueOrError`.

---

## Findings of the verified pass (summary)

- **Node layout.** A 16-byte, 8-aligned value with an 8-byte payload and a 1-byte tag is
  Lua 5.4's and Luau's `TValue` layout. Godot's `Variant` is 24 bytes (40 with doubles).
  Sources: `lua/lobject.h` (5.4), `luau-lang/luau VM/src/lobject.h`,
  `godotengine/godot core/variant/variant.h`.
- **Numeric width.** Lua 5.4 defaults to int64 and double; its `luaconf.h` describes
  32-bit int and float as the configuration "for restricted platforms". Godot's Variant
  stores `int64_t` and `double`. Luau stores double and has an int64 type. All three keep
  32-bit floats only for bulk vector data. Float32 represents integers exactly only to
  ±2^24, and Godot's docs give about 32–65 km as the single-precision limit for world
  positions (`tutorials/physics/large_world_coordinates`).
- **Symbols.** Godot's `StringName` interns strings, compares them by pointer, and
  precomputes a 32-bit hash (`core/string/string_name.h`). Baker matches this, with a
  64-bit hash.
- **Handles.** EnTT's default 32-bit entity id splits 20 bits of index and 12 of version,
  with a 32/32 64-bit option (`src/entt/entity/entity.hpp`). flecs ids are 64-bit: 32
  bits of index and a 16-bit generation (`include/flecs/private/api_defines.h`).
- **Arrays.** Lua 5.5 stores array values and tags separately to avoid padding (the
  `ltable.h` comment, the 2023-05-15 commit). That does not suit Baker, which hands out
  `std::span<const Node>`.
- **Toolchains.** The Android NDK ships only libc++ (`developer.android.com/ndk/guides/
  cpp-support`). libc++ added double `from_chars` in LLVM 20 (llvm-project PR #91651,
  `Cxx17Papers.csv`). ndk-build disables exceptions by default; CMake builds enable them.

---

## Imported report (unedited)

> The text below is the wider-access agent's report as delivered. Read it with the
> verification notes above.

### Research brief: references on core value types in mainstream game engines

#### 1. Executive Summary Table

| Decision | Mainstream Practice Found | Evidence Tier | Impact on Baker's Choice |
|---|---|---|---|
| 1. Tagged Value | Lua/Luau use 16B tagged unions (TValue). Godot 4 uses 24B/40B (Variant). UE uses dynamic properties (FProperty) or 32B FVariant / 24B FInstancedStruct. Squirrel uses 16B SQObject on 64-bit systems. | Primary | Supports. Baker's 16-byte, 8-byte aligned Node is optimal for cache efficiency and aligned with lightweight dynamic scripting paradigms. |
| 2. Numeric Width | Modern engines use double and int64_t for config, data, LWC, and economy. UE5 LWC converted core math (FVector, TTransform) and Blueprint Float to 64-bit double. | Primary | Supports Widen. Baker's planned move to 64-bit scalars (int64 / double) matches current engine and data format standards. |
| 3. Interned Symbols | Case-insensitive string interning with preserved display case is standard (UE FName). AAA engines use compile-time hashed string IDs (Naughty Dog SID, Bitsquid IdString32/64). Entries are immortal. | Primary | Contradicts (Partial). Baker should adopt case-insensitive matching for designer identifiers while preserving case for display, matching UE FName. |
| 4. Strings | Dynamic value types favor owned SSO strings or interning handles (FName). Localized text uses refcounted immutable strings (UE FText). EASTL defaults to basic_string (SSO) or fixed_string. | Primary | Neutral. Separate refcounted immutable strings are well-suited for scripting/config values, mirroring UE FText string buffers. |
| 5. Arrays | Fixed-size slice sharing and COW buffers are common in data/script runtimes (Godot Array/PackedByteArray). Engine C++ runtimes prefer inline TArray<T> or contiguous value spans. | Primary | Supports. Baker's inline layout after a 24-byte header provides overhead-free macro expansion and slice sharing. |
| 6. Refcounting & Threading | UE TSharedPtr defaults to ESPMode::ThreadSafe (atomic). High-performance job engines (Naughty Dog, Frostbite) avoid atomics in hot loops entirely, using arenas, handles, and immutable snapshots. | Primary | Supports. Single-threaded mutation with multi-threaded read after freeze eliminates atomic overhead in job systems while maintaining memory safety. |
| 7. Object Handles | 64-bit generational handles are standard in AAA. UE FWeakObjectPtr is 64-bit (32-bit Index + 32-bit SerialNumber). UE5 FObjectHandle / TObjectPtr uses 64-bit integer bit-packing. | Primary | Supports Widen. Widening Baker's handle from 32-bit (16+16) to 64-bit (32+32) is mandatory for modern AAA live-object counts (>65k objects). |
| 8. Error Handling | C++ exceptions are strictly disabled in shipping builds across all console SDKs, Unreal Engine, and EASTL. Non-throwing result types (UE TValueOrError) are standard. | Primary | Supports. Baker's exception-free build path with std::expected / non-throwing twins is required for shipping on console and mobile toolchains. |
| 9. Toolchains & C++20 | UE 5.3 switched default to C++20. Android NDK r26–r29 ship LLVM 17–20. Mobile deployment targets gate floating-point std::from_chars/to_chars. std::function and std::filesystem are avoided. | Primary | Supports (with Caveats). C++20 is fully viable on consoles and mobile, but std::filesystem and float from_chars require fallback polyfills on older mobile targets. |

#### 2. Evidence and Analysis by Decision

**Decision 1: Tagged Value (Node).** Mainstream engines split dynamic representations
into high-performance C++ property reflection or tagged unions for scripting runtimes.

- Squirrel (SQObject): Squirrel uses a 16-byte tagged object structure on 64-bit
  architectures, comprising a 32-bit type tag, padding, and an 8-byte payload union.
  "typedef struct tagSQObject { SQObjectType _type; SQObjectValue _unVal; } SQObject;"
  — Squirrel 3.1 Header (squirrel.h)
- AngelScript: AngelScript passes generic script arguments via asIScriptGeneric and
  internal stack buffers composed of asDWORD (32-bit words) or quadwords (64-bit units).
  Dynamic properties use explicit type identifiers (int typeId) paired with raw memory
  pointers or 64-bit primitives rather than a single monolithic tagged union type.
  — AngelScript Core API Documentation (v2.36.0)
- Unreal Engine (FVariant & FInstancedStruct): For dynamic script data, UE provides
  FVariant (32 bytes holding EVariantTypes tag and value union) and FInstancedStruct (24
  bytes wrapping a UScriptStruct pointer and heap buffer). Blueprint execution (FFrame)
  uses explicit FProperty pointers to dereference typed values directly from container
  buffers without storing a type tag inline with every value. — Unreal Engine 5.4 Source
  (Runtime/Core/Public/Misc/Variant.h)
- AAA Engines (Naughty Dog & Guerrilla Decima): Naughty Dog's runtime object model uses
  Lisp-based data definitions parsed into strongly typed C++ runtime structures.
  Tool-time entities use key-value property dictionaries. "The tool-time game object
  model falls into... Key-value store: 'Dictionary' of other properties... Runtime object
  model primarily concerned with functionality and performance." — Jason Gregory (Lead
  Programmer, Naughty Dog), Game Engine Architecture (3rd Ed., 2018)

Verdict for Baker: Baker's 16-byte layout matches Lua 5.4, Luau, and Squirrel, staying
significantly more cache-friendly than Godot's Variant (24/40 bytes) or UE's FVariant (32
bytes).

**Decision 2: Numeric Width.** Data formats, config systems, and spatial architectures
across mainstream engines have shifted to 64-bit representations.

- Unreal Engine 5 Large World Coordinates (LWC): In UE 5.0, core engine types
  transitioned from 32-bit float to 64-bit double precision to eliminate spatial jitter at
  distances greater than 10 km from the world origin. "Large World Coordinates introduces
  support for double-precision data variant types in Unreal Engine 5... In brief, the
  data type FVector is now double instead of float." — Unreal Engine 5.4 Documentation
  ("Large World Coordinates"). "Blueprint's Float used to be C++ float (32-bit). However,
  that was changed due to implementation of Large World Coordinates (LWC) around UE 5.1.
  FVector is a union of 3 components using double." — Tech Art Aid ("Better Blueprints:
  Efficient Data", 2024)
- Unreal .ini and FProperty Types: UE config files serialize text strings into
  FIntProperty, FInt64Property, FFloatProperty, or FDoubleProperty. FInt64Property and
  FDoubleProperty are used for timestamps, database persistent identifiers, and
  high-precision gameplay parameters.
- Engine JSON Libraries: RapidJSON (widely used across AAA engines) parses all numeric
  primitives into native int64_t, uint64_t, or double representations: "RapidJSON parses
  numbers into int, unsigned, int64_t, uint64_t, or double depending on range and
  format." — Tencent RapidJSON Documentation (v1.1.0)
- Hex Literals: Engines parse hex strings (e.g., 0xFFFFFFFF) as unsigned bit patterns
  (uint32_t or uint64_t) to prevent unwanted sign-extension into negative values during
  script evaluation.

Verdict for Baker: Widening Baker's numeric scalars from int32/float to int64/double is
strongly supported by industry standards across spatial math (UE5 LWC) and persistent data
formats.

**Decision 3: Interned Symbols.** Symbol interning in commercial engines relies on
global, immortal pools with fast comparison indices and case-insensitivity.

- Unreal Engine FName: FName wraps an index into a global, process-wide name pool
  (FNamePool). "Names are stored as a combination of an index into a table of unique
  strings and an instance number. Names are case-insensitive, but case-preserving (when
  WITH_CASE_PRESERVING_NAME is 1)." — Unreal Engine 5.4 API
  (Runtime/Core/Public/UObject/NameTypes.h). Internal Structure: Represented as
  FNameEntryId ComparisonIndex (32-bit), FNameEntryId DisplayIndex (32-bit), and int32
  Number (1-based instance suffix, e.g., Foo_3). Persistence & Threading: Entries in
  FNamePool are never freed during runtime. Read access is lock-free across worker threads
  using sharded chunk allocators (DisplayShards). Package Serialization: In .uasset files,
  names are saved as an array of UTF-8/UTF-16 strings in the package Name Table, and
  serialized references store a 32-bit index into that local table.
- Compile-Time Hashed String IDs:
  - Naughty Dog (SID): "Permits strings to be specified in source code... converting the
    string literal directly into a hashed integer value at compile time... static
    StringId sid_foo = internString("foo");" — Jason Gregory, Game Engine Architecture
    (3rd Ed., Section 5.2.2)
  - Bitsquid / Stingray (IdString32 / IdString64): Bitsquid uses 32-bit or 64-bit
    MurmurHash3 hashes computed at compile-time or asset build time. Collisions are
    checked during data compilation via global hash tables. "IdString64 uses MurmurHash64
    to hash strings at compile time. In release builds, only the integer hash is stored."
    — Niklas Frykholm, Bitsquid Architecture Briefs
- Case Sensitivity Norm: Designer-facing identifiers across mainstream engines (Unreal
  FName, Frostbite, CryEngine) are overwhelmingly case-insensitive for comparisons to
  prevent user errors where Health vs health causes broken bindings, while preserving
  original casing for UI rendering.

Verdict for Baker: Baker's choice of pointer equality and 64-bit hashing is correct, but
Baker should adopt case-insensitive comparisons with case-preserving display to better
align with designer workflows.

**Decision 4: Strings.** Engine data layers distinguish sharply between dynamic mutable
strings and localized dynamic text buffers.

- Unreal Engine (FString vs FText): FString: A mutable contiguous vector of character
  data (TArray<TCHAR>), using owned heap allocations and standard value semantics.
  FText: Used for localized, designer-facing text. Immutable and internally
  reference-counted (TSharedRef<FString>), allowing zero-copy transfers, localized key
  lookups, and runtime language swapping. — Unreal Engine 5.4 Documentation ("String
  Handling")
- EASTL (eastl::basic_string & fixed_string): EASTL avoids refcounted strings due to
  atomic multithreading overhead. eastl::basic_string utilizes Small String Optimization
  (SSO, 24 bytes inline buffer on 64-bit builds). eastl::fixed_string allocates its
  character array directly on the stack/container without dynamic heap allocation. —
  Electronic Arts EASTL Documentation (EASTL/string.h)

Verdict for Baker: Baker's refcounted, single-allocation immutable string model aligns
well with dynamic script values and mirrors the string buffer model used in UE FText.

**Decision 5: Arrays.** Commercial engines organize heterogeneous scripting arrays
around reference-counted slice handles or flattened property buffers.

- Godot Engine (Array & PackedByteArray): Godot 4 Array structures use refcounted shared
  copy-on-write (COW) storage buffers. Modifying an array slice detaches the buffer if the
  reference count is greater than 1. — Godot 4.3 Documentation ("Variant and Array
  implementation")
- Unreal Engine Data Arrays: Unreal Blueprint arrays (FScriptArray) wrap untyped
  contiguous memory buffers. Macro expansions and property merges construct new arrays
  in-place rather than performing shared slice tracking.

Verdict for Baker: Baker's refcounted fixed-size array layout with inline children
sitting after a 24-byte header provides ideal copy-on-write semantics for macro expansions
and data merging.

**Decision 6: Refcounting and Threading.** High-performance engines avoid atomic
reference counting in hot job-system execution loops due to hardware bus locking
overhead.

- Unreal Engine TSharedPtr Default: UE TSharedPtr defaults to atomic reference counting
  (ESPMode::ThreadSafe) engine-wide. "ESPMode::ThreadSafe is the default mode for
  TSharedPtr and TSharedRef in Unreal Engine." — Unreal Engine 5.4 Source
  (Runtime/Core/Public/Templates/SharedPointer.h)
- EASTL Atomicity Defaults: EASTL shared_ptr uses interlocked atomic counters by default
  (EASTL_FORCE_INTERLOCKED). However, eastl::intrusive_ptr leaves the increment/decrement
  implementation to the underlying object, permitting non-atomic implementations. —
  Electronic Arts EASTL Documentation (EASTL/shared_ptr.h)
- Job System Execution Models: Modern fiber-based job systems (Naughty Dog, Frostbite,
  Insomniac) do not use atomic reference counting to share data across worker threads.
  "We run a fiber-based job system... Data passed to worker jobs is stored in transient
  linear arenas or handle arrays. Immutable data snapshots are prepared on the main thread
  and read by worker threads without refcounts or atomic locks." — Christian Gyrling
  (Lead Programmer, Naughty Dog), GDC 2015 ("Parallelizing the Naughty Dog Engine using
  Fibers")
- Measured Costs of Atomic Refcounting: Atomic instructions (lock xadd on x86,
  ldrex/strex or ldadd on ARM64) force L1/L2 cache line bouncing across CPU cores.
  Measured overhead shows atomic counter operations cost ~15–50 CPU cycles compared to ~1
  cycle for non-atomic modifications under core contention.

Verdict for Baker: Baker's approach—single-threaded mutation with multi-threaded read
access after freezing—is superior for multi-threaded job systems compared to default
atomic reference counting.

**Decision 7: Object Handles.** Commercial engines rely on 64-bit generational handles to
prevent stale handle dereferencing across high live-object counts.

- Unreal Engine FWeakObjectPtr: Unreal Engine's weak pointer layout uses 64 bits total:
  "Weak pointers only store two things: int32 ObjectIndex and int32 ObjectSerialNumber."
  — Epic Developer Community ("All about Soft and Weak pointers", 2024). ObjectIndex
  indexes into GUObjectArray (32-bit), while ObjectSerialNumber (32-bit) acts as the
  generation counter to detect recycled slots.
- Unreal Engine 5 FObjectHandle / TObjectPtr: In UE 5.0+, raw UObject* pointers in
  property definitions were replaced with TObjectPtr<T>. On 64-bit systems, FObjectHandle
  wraps a 64-bit integer (uint64) that bit-packs either an absolute memory pointer or an
  asset path index for lazy streaming. — Unreal Engine 5.4 CoreUObject Documentation
- Bungie Datum Index: Bungie's object identification system uses 32-bit generational
  handles: "A datum index is a 32-bit integer split into a 16-bit array index and a 16-bit
  salt (magic number). When an object is deleted, the salt for that slot is incremented."
  — Chris Butcher (Bungie), Game Tech 2004 ("Building a Data-Driven Game Engine")
- Live Object Bounds: Modern open-world titles routinely exceed 65,536 (2^16)
  simultaneous entity instances. A 16-bit index overflow crashes or corrupts salt
  validation. 32-bit index + 32-bit generation (64-bit total) is required for modern
  console/PC scale.

Verdict for Baker: Baker must widen its handle design from 32-bit (16+16) to 64-bit
(32-bit index + 32-bit salt) to avoid index exhaustion on modern titles.

**Decision 8: Error Handling.** C++ exceptions are disabled across all mainstream game
engines and console platform toolchains.

- Unreal Engine Exception Policy: Unreal Engine explicitly compiles with exceptions
  disabled (-fno-exceptions / /EHsc-) across all desktop, console, and mobile platforms:
  "PLATFORM_EXCEPTIONS_DISABLED is defined to 1 for all shipping targets." — Unreal
  Engine 5.4 Build System (HAL/Platform.h)
- EASTL Exceptions Default: "EASTL_EXCEPTIONS_ENABLED is defaulted to 0... EASTL relies
  on user-defined assertions or failure handlers rather than C++ try/catch blocks." —
  EASTL FAQ Documentation
- Minimum Toolchain Requirements for std::expected: GCC (libstdc++): GCC 13.1+ (May
  2023); LLVM (libc++): LLVM 16.0+ (March 2023); MSVC (MSVC STL): Visual Studio 2022
  v17.5+ (February 2023)
- Engine Monadic Result Types: Unreal Engine uses TValueOrError<ValueType, ErrorType>
  defined in Templates/ValueOrError.h as an exception-free expected monad.

Verdict for Baker: Disabling exceptions and using std::expected (or a C++20 standard
fallback) matches standard practice across console and mobile game development.

**Decision 9: Toolchains and C++20.** C++20 adoption is established on desktop and
current consoles, but mobile standard library targets present platform-specific
constraints.

- Android NDK LLVM Versions: NDK r26 (September 2023): LLVM 17.0.2 (clang-r487747e);
  NDK r27 (July 2024): LLVM 18.0.1 (clang-r522817); NDK r28 (February 2025): LLVM
  19.0.0 (clang-r530567b); NDK r29 (September 2025): LLVM 20.0.0 (clang-r563880c) —
  Android NDK Wiki / Release Notes
- Apple libc++ Availability Cutoffs: std::filesystem: Available on iOS 13.0+ / macOS
  10.15+ (Xcode 11+). Floating-point std::from_chars / std::to_chars: LLVM libc++ added
  floating-point parsing in LLVM 20. On Apple platforms, this feature is gated behind
  macOS 15.0+ / iOS 18.0+ deployment targets. Supporting older iOS versions requires a
  lightweight fallback parser (e.g., fast_float).
- Unreal Engine C++20 Default: Unreal Engine 5.3 officially switched the target default
  to C++20: "Unreal 5.3 is built with C++20, so using features such as concepts is
  enabled by default... CppStandard = CppStandardVersion.Cpp20;" — Unreal Engine 5.3
  Release Notes / Developer Forums
- Console Compiler Toolchains: PlayStation 5: Sony PS5 SDK uses a custom Clang/LLVM
  toolchain. Xbox Series X/S: Microsoft GDK uses MSVC (Visual Studio 2022) or clang-cl.
  Nintendo Switch / Switch 2: Nintendo SDK utilizes Clang/LLVM with libc++.
- Engine Guidance on Avoiding std::function and std::filesystem: std::function:
  Disallowed in high-performance engines due to dynamic heap allocations when lambdas
  exceed small-buffer optimization thresholds (16–32 bytes), lack of custom allocator
  integration, virtual call overhead, and binary bloat. Engines use non-allocating
  delegates (e.g., Unreal TDelegate / TFunctionRef). std::filesystem: Disallowed due to
  heavy heap allocation during path manipulation, mandatory wide-string conversions on
  Windows (std::wstring), throwing error overloads, and complete incompatibility with
  engine Virtual File Systems (VFS), package archives (.pak), and compressed asset
  bundles.

Verdict for Baker: Baker's requirement for C++20 is fully supported across PC, console,
and mobile. However, Baker must replace std::filesystem with a custom VFS abstraction and
provide a fallback for floating-point from_chars on older iOS targets.

#### 3. Open or NDA-Blocked Questions & Public Proxies

- Console Standard Library Support for Floating-Point std::from_chars: NDA Restriction:
  Direct console SDK header files (PS5, Switch 2) cannot be publicly disclosed or cited
  under non-disclosure agreements. Public Proxy: The public toolchain alignment for PS5
  (Clang/LLVM) and Xbox Series X/S (MSVC STL) matches LLVM libc++ and MSVC STL release
  timelines. Because LLVM libc++ only finalized float from_chars in LLVM 20, developers
  targeting console/mobile OS builds using older LLVM runtimes must integrate standalone
  libraries such as fast_float.
- Decima Engine & Naughty Dog Internal Dynamic Type Headers: NDA / Proprietary Code
  Restriction: Exact C++ struct alignments and memory layouts for proprietary studio
  reflection systems (Guerrilla Decima, Naughty Dog Runtime) are not open-source. Public
  Proxy: Published GDC presentations (Christian Gyrling GDC 2015) and engineering
  textbooks (Game Engine Architecture 3rd Ed. by Jason Gregory) provide public
  confirmation that these engines rely on fixed-offset C++ structs, 32/64-bit hashed
  string IDs, and continuous arena allocators rather than heavyweight dynamic std::any
  variants.

#### 4. Reference List

Primary Sources

- Android NDK Revision History & Wiki Changelogs (r26–r29): github.com/android/ndk/wiki
- Bungie Engineering Talks (Chris Butcher, Game Tech 2004): "Building a Data-Driven Game
  Engine: Bungie's Datum Array Architecture"
- Electronic Arts EASTL Repository & FAQ: github.com/electronicarts/EASTL
- Epic Games Developer Documentation — Unreal Engine 5 API (FName, FWeakObjectPtr,
  TObjectPtr, LWC, CVars): dev.epicgames.com/documentation/unreal-engine
- Jason Gregory — Game Engine Architecture (3rd Edition, CRC Press, 2018):
  gameenginebook.com
- Naughty Dog GDC Talks (Christian Gyrling, GDC 2015): "Parallelizing the Naughty Dog
  Engine using Fibers"
- Squirrel Scripting Language Source & Headers:
  repo.or.cz/openttd-github.git/blob/src/3rdparty/squirrel/include/squirrel.h

Secondary Sources

- George Prosser Engineering Blog: "Understanding Weak Pointer Implementations in UE5":
  prosser.io/blog
- Tech Art Aid: "Better Blueprints: Efficient Data in UE5":
  techartaid.com/posts/better-blueprints-data
- UnKnoWnCheaTs Architectural Reversing: "Unreal Engine FNamePool and GNames Layouts":
  unknowncheats.me

Anecdotal Sources

- Epic Games Developer Forum Threads: "Default member initializers for bit-fields
  requires at least /std:c++20": forums.unrealengine.com
