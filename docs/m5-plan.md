# M5 plan: objects

Draft for review before any M5 code. Spec §5 ("Objects", the object parts of "Native
function binding") and §10 (M5 row) are the requirements. The talk digest (Appendix A)
fills in the original's `DataObject` and `DataClass`. The open questions at the end change
the design. Once they are answered, the decisions go into `docs/decisions.md` (D48 onward)
and this file is deleted.

## Scope

From the M5 row:

- `Object<T>` and `Binder`, with method and property binding;
- inheritance flattening;
- namespace lookup;
- script-side `{class}` and `{new}` (`{func}` came early, in M4: D46).

Done when:

1. Both dispatch directions work: script calls a C++ method, and C++ calls a handler that
   script defined.
2. The talk's `Person::grow` example runs from script and from C++.

Carried from §5 and M4:

- object commands evaluate slot 0 (`{{nearest_object $pos} suffer_damage}`);
- salted handles with strong (`get`) and weak (`try_get`) resolution;
- an unhandled method is an error naming the type, the method and the call site;
- `[...]` property nodes evaluate (an error until now, D47);
- `$this` is bound after argument evaluation;
- methods appear in the generated reference under their owning type;
- `check` adds unknown methods to the demand report where the object's type is known
  statically.

Not in M5:

- typed messages and the allocation-free C++→C++ path (M6);
- tasks (M7).

## Design

### Object table and handles (D4, D19)

- **The table.** Each Context keeps a table of slots, each holding:
  - the object's address;
  - its type record;
  - a 32-bit salt;
  - an optional name.
- **Handles.** A `Node::object(handle)` holds the slot index and salt. Resolving one
  checks the bounds and the salt.
  - `ctx.get<T>(node)` errors on a stale handle or a wrong type.
  - `ctx.try_get<T>(node)` returns null in those cases.

  Removing an object bumps its slot's salt, so old handles fail cleanly instead of
  dangling. Freed slots are reused.
- **Names.** A name like `bob` resolves through a per-Context table indexed by
  `Symbol::index()`, like functions (D44).
  - Functions win: `{bob grow 10}` dispatches to the object only if `bob` is not a function.
  - Registering an object under a function's name, or under a name already taken, is an
    error.
- **No RTTI.** Each bound type has one static `TypeRecord`. `try_get<T>` compares type
  records, walking the flattened ancestor list for base types.

### `Object<T>` and `Binder<T>` (spec §5)

```cpp
struct Person : moira::Object<Person> {
    float height = 6.0f;
    float on_grow(float amount) { height += amount; return height; }
    static void bind(moira::Binder<Person>& b) {
        b.inherits<Creature>();
        b.method("grow", &Person::on_grow).doc("Grows by `amount`.");
        b.property("height", &Person::height);
        b.method("shrink", [](Person& self, float a) { self.height -= a; });
    }
};
ctx.add(bob, "bob");                    // or ctx.add(bob) for a nameless object
```

- **Method tables.** Built once per type, lazily and thread-safely, from `T::bind`, into a
  flat array of (Symbol, thunk) pairs sorted by symbol. Dispatch is a binary search.
  - `inherits<B>()` copies B's entries in at build time, so dispatch never walks a chain.
  - A derived method replaces the base method of the same name.
- **Method thunks** reuse M4's binding: signature deduction, `Convert<T>`, `Raw<T>`,
  `std::optional`, `Args`, overloads, tiers, status, and docs. Member function pointers
  and lambdas taking `T&` first both work.
- **Properties.** `b.property(name, &T::member)` gives get and set through the same
  converters, plus a getter/setter pair form for computed values.

### Dispatch

- **Object commands.** A command's slot 0 dispatches to an object when it is:
  - an `Object` node;
  - a `Var` or command that evaluates to one;
  - or a symbol naming an object (and not a function).

  The method is slot 1.
- **Lookup order.**
  1. Script handlers attached to the object's class (Q2).
  2. The native method table.
  3. The built-in object methods: `get`, `set`, `type`, `name`.
  4. Otherwise: "Person has no method 'fly'", located.
- **`$this`.** Bound, after argument evaluation, to the object for the duration of any
  handler, by the same dynamic-scope mechanism as `do`.
- **`[...]` property nodes** read a property path from `$this`. `[height]` is
  `{$this get height}`; `[pos x]` reads `x` from the `pos` property.

### Script classes and instance parameters (talk: `DataClass`, Q2 and Q3)

```
{class Person
  (grow ($n) {set [height] {+ [height] $n}})
  (miss {print "whoosh"}))
{new Person Bob (height 6)}       ; a named instance with its parameters
{Bob grow 2}
```

- `{class Name handlers...}` defines a set of script handlers. When `Name` is also a bound
  native type, the handlers attach to that type (`DataClass`); otherwise `Name` is a
  script-only class.
- `{new Class [name] (param value)...}` creates an instance owned by the Context.
  `{delete obj}` removes it, and its handles go stale.
- Instance parameters are a loose key–value array per object, allocated on first `set`.
  `get` and `set` reach a bound property first, then the parameters.

### The C++ direction

- **Calling.** `ctx.call(obj, "grow", 10)` (and `try_call`) builds the command and
  dispatches it, so it reaches a script handler as readily as a native method. That is
  done-when item 1, the C++→script direction. Building the command allocates. The
  allocation-free C++→C++ path is M6's typed messages; `call` stays as the general form.
- **Lifetime.** `Object<T>` stores the Context and handle it was added with, and its
  destructor removes it (Q1). An object destroyed in C++ therefore turns every script
  reference into a clean "stale handle" error.

### Tooling

- **The reference** gains a Types section: each bound type with its base types, methods
  (signatures, docs), and properties. Script classes are listed with their handlers.
- **`check`** reports a call to a method a type does not have, when the object is a named
  object or a `{new}` in the same tree whose type is known. It also adds unknown methods
  to the demand report. Dynamic receivers are skipped, as unregistered heads are.

## Budget

| Area | Estimated lines |
|---|---:|
| Object table, handles, names, strong/weak resolution | 150 |
| `Object<T>`, `TypeRecord`, `Binder`, method and property thunks, flattening | 250 |
| Dispatch, slot-0 evaluation, `$this`, `[...]`, built-in methods | 150 |
| `{class}`, `{new}`, `{delete}`, instance parameters | 150 |
| Reference and `check` additions | 80 |
| **Total** | **~780** |

That takes the core from 6,203 to about 6,980 of 8,000, leaving about 1,000 lines. Rough
estimates for what follows:

| Milestone | Estimated lines |
|---|---:|
| M6: messages, call stack, trace, call statistics | 400 |
| M7: tasks and the scheduler | 400 |
| M8: binary cache | 400 |
| M10: core instrumentation | 300 or more |
| **Total** | **~1,500** |

At the current rate the budget runs out during M7. Q4 asks how to handle that.

## Open questions

**Q1. How does a C++ object enter and leave a Context?**
- **Recommended.** `ctx.add(obj[, name])`. `Object<T>` keeps the Context and handle
  (16 bytes per object), and its destructor removes the entry. A destroyed object can
  never be reached through an old handle.
- **Alternative.** No back-pointer: the owner calls `ctx.remove(obj)`. This is smaller,
  but forgetting the call leaves a dangling entry that only the salt can catch, after the
  slot is reused.

**Q2. Script handlers on native objects, and their order.**
- **Recommended.** `{class Person ...}` naming a bound native type attaches script
  handlers to it, as the original's `DataClass` did. Script handlers are tried before
  native ones, so data can override or extend behavior without a rebuild.
- **Native first.** Script handlers only fill in methods the type lacks: safer, but data
  cannot override.
- **Separate.** Script classes and native types stay apart: smaller, but loses the
  original's main use of script handlers.

**Q3. Instance parameters.**
- **Recommended.** Every object can carry a loose key–value parameter array (the talk's
  "Perl dictionary"), allocated on first use. It is reached by `get`/`set` and `[...]`
  after bound properties.
- **Alternative.** Bound properties only. Script classes then declare fields up front,
  which is stricter, but a native object cannot carry script state.

**Q4. The line budget.**
- **Recommended.** Build M5 as planned and decide in the M6 plan, with M5's actual size in
  hand: raise the budget, or move parts out of the core (as shapes already are).
- **Raise now.** Raise to 10,000 now and stop flagging until then.
- **Trim M5.** Defer `{class}`/`{new}` script-only classes, which the talk says they
  rarely used (about −100 lines), keeping script handlers on native types.

**Defaults, no answer needed.**
- Method tables are static per type, not per Context: C++ bindings cannot differ between
  Contexts.
- Names are unique per Context, and functions take precedence over objects.
- A handle's salt starts at 1 and increments on each reuse of the slot, so a zero handle
  is never valid.
- `ctx.call` reports errors the way `try_eval` does, including exceptions thrown by a
  method.
