# The Moira language

This guide is the language as the person writing `.dta` files meets it: data, the
preprocessor, packs and levels, shapes, script, and errors. The C++ side appears where
it explains the data. Every built-in function is listed, with its signature, in the
generated [script reference](script-reference.md); the patterns that put the pieces
together are in the [cookbook](cookbook.md).

- [Data](#data)
- [The preprocessor](#the-preprocessor)
- [Packs and levels](#packs-and-levels)
- [Shapes](#shapes)
- [Script](#script)
- [Errors](#errors)

## Data

A file is a sequence of nodes. Most of them are arrays:

```dta
; A comment runs to the end of the line. /* This kind */ may span lines.
(menu
   ('bacon and eggs'
      (price 12.75)
      (calories 120))
   ('fruit and cereal'
      (price 11.75)
      (calories 12)))
```

### Nodes

| Kind | Written | Notes |
|---|---|---|
| Int | `42`, `-7`, `0x1F` | 64-bit. A hex literal is its unsigned value (D18). |
| Float | `12.75`, `1e-3` | A double; it must be finite. |
| Symbol | `price`, `'bacon and eggs'` | Interned: comparing two is comparing pointers. Quote one that has spaces or punctuation. |
| String | `"Over here!"` | Two escapes: `\n` for a newline, `\q` for a double quote. |
| Array | `(tag values...)` | Data. |
| Command | `{function args...}` | Script: runs when its value is read ([Script](#script)). |
| Property | `[name]`, `[pos x]` | A property of `$this`, in a method. |
| Variable | `$name` | A script variable's value. |

There is no boolean literal. `bool` reads an integer: `0` is false and any other true.
DTA's traditional `TRUE` and `FALSE` are macros for `1` and `0` (D85), so define them if
your data uses them: `[TRUE 1] [FALSE 0]`.

### Tags and lookup

An array whose first node is a symbol is *tagged* by it, and the nodes after the tag are
its *values*. Lookup walks tags: in the file above, `menu` has two children, tagged
`'bacon and eggs'` and `'fruit and cereal'`, and each of those has `price` and
`calories`. A tagged array with one value is how a setting is written: `(price 12.75)`.
With several, `(clear_color 0.1 0.1 0.2)`, a reader takes them all (a `float[3]`, a
`std::vector`) or one by index.

A **path** names a value from the root, by tags and indexes, separated by `.` or `/`:

```
menu.'bacon and eggs'.price      12.75
renderer.clear_color.2           0.2: indexes count from 0 after the tag
menu/'bacon and eggs'/price      the form errors print, so a path can be pasted back
```

From C++, typed reads return `Expected<T>`: the value, or an error naming the key, the
types, the file, the line, and the path. A fallback is used only when the key is absent;
a value of the wrong type is still an error, because silently ignoring a mistyped value is
the failure located errors exist to prevent.

```cpp
auto root = moira::read_file("menu.dta");                     // Expected<Ref<Array>>
double price = *(*root)->get<double>("menu.'bacon and eggs'.price");
int fat = *(*root)->get<int>("menu.'bacon and eggs'.fat", 0);  // absent: 0
const moira::Array* eggs = (*root)->try_get("menu.'bacon and eggs'");   // null if absent
auto cals = eggs->find_int("calories");                       // one level, by tag
```

`Convert<T>` teaches lookups your own types; vectors, fixed arrays, and
`std::string` (with `<moira/std_interop.hpp>`) are built in. When an array has several
children with the same tag, lookup finds the first; `subarrays()` visits them all.

Every array remembers its file and line, so a value can always say where it came from.

### Merging

Two trees merge by tag, which is how a Config stacks layers and how `#merge` works. For
each tagged array of the upper tree, the array with the same tag below is merged into
recursively; everything else (the values) replaces what was below. So
`(width 3)` over `(width 2 (range 0 10))` gives `(width 3 (range 0 10))`: a layer changes
a value without restating its metadata. A tag the lower tree lacks is added.

## The preprocessor

Directives are handled while a file is read and never appear in the tree. Files read
through one *session* (`LoadSession`, or a Config's `session()`) share defines and macros,
in the order they are read; a Config's layers read in one session, bottom to top.

| Directive | Effect |
|---|---|
| `#include path` | Splice another file's top-level nodes here. |
| `#include_opt path` | The same; a missing file is not an error. |
| `#require path` | The same, once per session: later `#require`s of that file, from any file, are no-ops. |
| `#merge path` | Merge another file into the enclosing array, keeping existing keys and adding missing ones. |
| `#ifdef N`, `#ifndef N`, `#else`, `#endif` | Conditional sections. |
| `#define N` | Define a flag, for `#ifdef`. |
| `#define N (body)` | Define a macro whose body is the array's contents. |
| `#undef N` | Remove a flag or macro. |
| `#autorun {command}` | Collect the command into the session's `autorun()` list, for the host to run after loading. |
| `[N node ...]` | At the top level of a file: define macro N. |

Paths may be quoted (`#include "shared/common.dta"`) or bare, and resolve against the
including file's directory, then the session's include directories
(`add_include_dir`).

### Macros

A macro is a named sequence of nodes, spliced wherever its name appears as a bare symbol:

```dta
[RED 1 0 0]
[HANDLER (hit {fall_down})]
(barrel (color RED) HANDLER)
(crate  (color RED) HANDLER)
```

`barrel` reads as `(barrel (color 1 0 0) (hit {fall_down}))`. Splicing shares the macro's
arrays rather than copying them. A macro defined in one file serves every later file of the
session, which is what a file of shared definitions is for.

### Platforms and builds

Defines usually come from the host, so one file serves every target:

```dta
(renderer
#ifdef HANDHELD
   (shadows 0)
#else
   (shadows 1)
#endif
)
```

```cpp
cfg.session().define("HANDHELD");   // before pushing the layers
```

### `#require`

`#include` splices a file every time. `#require` splices it the first time the session sees
it; afterwards it is a no-op, from any file and any later read through that session (D81).
Use it for a file several others depend on, such as a pack of shared macros: required from
three files, it is read once, and its macros serve all three.

## Packs and levels

A *pack* is a file that declares itself one, as its first form:

```dta
(pack boss_tools (level game) (requires combat_core))
[boss.restart_phase {boss_phase_restart}]
```

The host names the levels, hardest first; a pack's level is its rank among them (the
spec calls this its layer; Moira says *level*, because a Config's layers are its stack of
files, D80):

```cpp
cfg.session().set_pack_levels({"engine", "game", "task"});
```

The form is recorded (`session.packs()`, `session.pack_of(file)`) and left out of the
tree. A pack without a level, or with a level not in the list, is an error at load.

**Harness metadata.** A pack may also say who keeps it, whom it is for, and how far it has
evolved, from an idea to a commodity:

```dta
(pack boss_tools (level task) (requires combat_core)
   (owners "td-encounter" "td-combat") (for designers) (stage custom))
```

`owners` and `for` list names (strings or symbols); `stage` is `genesis`, `custom`,
`product`, or `commodity`, and anything else is an error at load. They are kept in the
`Pack` the session records, for tools and reports; nothing else depends on them.

**The one-way rule.** A harder pack may not depend on a softer one: engine code does not
reach into one task's harness. Moira warns, never fails, on two kinds of reach:

- **Macros**, while reading: a pack's file expanding a macro that a softer pack defined
  is a warning in `session.warnings()`, once per file and macro, with file and line
  (D82). A Config forwards it to its warning handler.
- **Calls**, in `check`: a call, in a pack's file, to a script function defined in a
  softer pack's file is a warning in `CheckReport::warnings` (D83). Pass the session:
  `moira::check(tree, ctx, report, &cfg.session())`.

Softer using harder is the point of the levels, and passes silently; so does everything
in files that declare no pack, so a small project pays nothing. By convention a pack's
names carry a dotted prefix (`boss.restart_phase`), which the reference and tuning panels
group by.

### Generated files: `origin`

A file that a tool generates, such as an export from a spreadsheet, says so in its first
form (or the first after its pack declaration):

```dta
(origin "economy.xlsx" (tool sheets-export))
(econ (price 5) (tax 2))
```

The form is recorded (`session.origin_of(file)`) and left out of the tree, and a Config's
`source()` reports `origin` and `tool` for every value the file supplies, so a number can
be traced to the spreadsheet it came from. The first value must be a string, so ordinary
data such as `(origin 0 0 0)` is not taken for one (D91).

## Shapes

A shape says what an array should look like: its fields, their types, and their ranges.
Shapes are declared in data, in one `(shapes ...)` container, each tagged by its own name
(D84):

```dta
(shapes
   (boss_phase (at bosses * phases)
      (enter_at float (range 0 1) (kind percent) (desc "HP fraction that starts it"))
      (attacks  (list symbol))
      (cadence  float (range 0.2 5) (optional))))
```

- **Types:** `int`, `float`, `bool`, `symbol`, `string`, `any`, `(list T)`, or another
  shape's name.
- **Metadata:** `range`, `step`, `kind`, `desc`, `note` (as a tweak's), and `optional`.
  A field without `optional` is required.
- **What a shape governs:** every array tagged with its name, anywhere in the tree. `(at
  ...)` narrows it to arrays whose ancestors' tags, from the root, are the ones listed;
  `*` matches any one tag. The example above governs `(phase1 ...)` in
  `(bosses (dragon (phases (phase1 ...))))`.
- **Keyed children are not reached** (D85). In `(bosses (dragon ...) (golem ...))` each
  boss is tagged by its own name, so no shape named `boss` governs them. Write data a
  shape should check as `(boss (name dragon) ...)`, or narrow with `(at ...)` to the
  children of a known parent, as `boss_phase` does.
- **Layers merge shapes like any data**, because the container is tagged `shapes` and
  each shape by its name: a later layer adds a shape, adds a field, or retunes one
  field's range.

Checking reports a field of the wrong type or out of range, or a required field missing,
as an error, located by file, line, and path. A field the shape does not declare is a
warning, so adding data never waits on its shape. Commands and variables are not checked:
their values are unknown until they run.

```cpp
cfg.set_validator(moira::shape::validator());   // checked at every push, reload, and restore
```

Through a Config, violations go to the warning handler; the load itself never fails on a
shape. Elsewhere, `moira::shape::Set` reads the declarations and `moira::shape::check`
checks a tree; `set.shapes()` enumerates them, with their metadata, for a tool to build
panels from. Shapes are opt-in, and cost nothing unless checked.

## Script

Commands are arrays in braces: `{function args...}`, or `{object method args...}`. A
command in data is a value computed when it is read:

```dta
(enemy
   (grunt (hp {* 30 $difficulty}))
   (brute (damage {if {> $difficulty 2} 12 9})))
```

Reading `enemy.grunt.hp` as an int from C++ evaluates the command, in the thread's current
`Context`, and converts the result. Reading the node raw (`try_find`, `operator[]`) gives
the command itself, unevaluated: that is how handlers are stored.

### Evaluation is lazy

Running `{f a b}` finds `f` and hands it the command; nothing evaluates `a` or `b` until
`f` reads one, by type. So `if`, `set`, `do`, `and`, and `or` are ordinary functions, not
keywords: `if` reads its condition and evaluates only the branch it takes. There are no
special forms, which is why the whole language is the [reference](script-reference.md)'s
list of functions.

The built-ins are control (`if`, `do`, `set`, `func`, `override`, `class`, `new`,
`delete`), arithmetic (`+ - * / %`), comparison (`== != < <= > >=`), and logic (`and or
not`). Ints stay ints (wrapping on overflow) until a float joins them; `{/ 7 2}` is 3.5.
Everything else is what the host binds:

```cpp
moira::Context ctx;
ctx.func("spawn", [](moira::Symbol kind, int64_t count) { /* ... */ return count; });
ctx.global("difficulty", 2, "Set by the game: 1 to 3.");
```

A native function's parameter and result types are deduced from its signature; it can
take `Raw<Node>` for an argument it evaluates itself, `std::optional<T>` for an optional
one, `Args` for the rest, and registering the name again with another signature makes an
overload set. A call with two scalar arguments takes about 31 ns and never allocates
(D78).

### Variables

`$name` reads a variable. Variables are **dynamically scoped**: `{do (($a 1) $b) body...}`
binds `$a` to 1 and `$b` to unset for the body, and restores both afterwards, even on an
error. A function sees, and can change, the variables of whoever called it. That is
deliberate (it is the original design, and why reading `$a` is one load), so do not treat
it as lexical scope. `{set $a value}` stores; the host reads and writes the same cells with
`ctx.var("a")`.

### Functions

```dta
{func scaled ($base) (doc "Scales by difficulty.") {* $base $difficulty}}
{func old_spawn ($kind) (status deprecated spawn) {spawn $kind 1}}
```

A script function binds its arguments like `do`, runs its body, and returns the last
value. `(status stub)`, `(status provisional)`, `(status deprecated replacement)`, and
`(doc "...")` may follow the parameters; status shows in the reference and in `check`'s
demand report. A script function may not replace a native one unless written
`{override name ...}`, which logs it.

### Objects and classes

A C++ type derived from `moira::Object<T>` binds methods and properties once, in a static
`bind`, and is added to a Context by name:

```dta
{grunt dash 3}          ; a C++ method
{grunt get hp}          ; a property; {grunt set hp 50} stores one
{grunt hit 20}          ; a typed message's script face
```

`{class T (name ($params) body...) ...}` attaches script handlers to a type, C++ or
script-only; handlers come before native methods, for script and C++ senders alike, so a
designer can change one type's response without touching its C++. In a handler, `$this` is
the object, and `[name]` reads its property or loose parameter (`{set [hp] 5}` stores
one). `{new T name (param value) ...}` makes an object of a script class or a bound type,
and `{delete obj}` removes it.

A command sent to a destroyed object is a clean error: Contexts refer to objects by salted
handles, never pointers.

### Tasks

The default scheduler is an object named `scheduler`:

```dta
{scheduler delay_task 2 {play_sfx "ding"}}           ; once, 2 units from now
{scheduler interp_task $fade 0 100 {set_alpha $fade}} ; $fade from 0 to 100 over 100 units
{scheduler thread_task {open_door} {wait {door_open}} {wait_for 1} {close_door}}
```

A task captures, when it is made, the value of every bound variable it names, so a task
made inside `{do (($n 5)) ...}` still sees `$n` after the `do` has returned. Each returns
a handle for `{scheduler cancel $h}`. A task made in a handler ends when its `$this` object
is removed. The host owns time: `ctx.scheduler().tick(dt)`, once a frame, in its own
units. C++ tasks are coroutines on the same scheduler (`co_await moira::delay(t)`).

### Checking before running

`moira::check(tree, ctx)` walks data without evaluating anything and reports every call
with the wrong number of arguments for every overload, or a literal argument of the wrong
type. Arguments that are commands or variables are skipped: their types are unknown until
run. Its **demand report** lists the calls to functions nobody has written yet, and to
stubs, with every site: the backlog the data is asking for.

```
moira: 'spawn' takes 2 arguments, given 1  (rules.dta:11)
demand: 'play_sfx' is not defined, called at rules.dta:11
```

Run it in CI over every data file, and on every reload in a development build.

## Errors

Moira never throws: every call that can fail returns `Expected<T>` (D54), whose error is
a structured `Error`: a code, a message, and where it happened. Its one-line form is

```
moira: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
```

read as: what went wrong; the file and line of the array involved; and the path from the
root to it, which can be pasted back into a lookup. A parse error names the line where
the trouble started, so an unclosed parenthesis points at its opening line.

An error raised in script also carries the script calls it unwound through, innermost
first. `describe(error)` prints them:

```
moira: '%' by zero  (boss.dta:9)
  in {%}  (boss.dta:9)
  in {bob attack}  (boss.dta:4)
```

Read it top down: the failing call, then each caller, each with the file and line where it
is written. `script_backtrace()` prints the same stack while a call is running, for an
assert handler or crash reporter to print beside the C++ stack.
