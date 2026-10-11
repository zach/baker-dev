# M4 plan: Context, evaluation, binding

Draft for review before any M4 code. Spec §5 and §10 (M4 row) are the requirements; this
document says how I intend to meet them, what it costs, and where the spec leaves a choice.
The open questions at the end are the ones whose answers change the design.

**Answered 2026-10-11: the recommended option on Q1–Q4.** The decisions move into
`docs/decisions.md` (D44 onward) when M4 starts, and this file is deleted then.

## Scope

From the M4 row:

- `Context`, variables, dynamic scoping, lazy command evaluation.
- Native `func` binding with signature deduction, `Raw<T>` parameters, overload sets.
- Generated script reference (`docs/script-reference.md`), checked for drift in tests.
- `moira::check`: wrong arity caught at load without evaluating, plus the demand report.

Done-when items, each a test or example:

1. The calculator example (`examples/calculator`).
2. The binding benchmark: a native call with 2 scalar args in ≤ 40 ns, with zero
   allocations.
3. `if` and `set` are ordinary registered functions. The evaluator has no case for either.
4. The untaken branch of `if` is never evaluated.
5. `script-reference.md` generates and is checked for drift.
6. `check` reports a wrong-arity call to a registered function, without evaluating.
7. A script stub replaced by a native registration of the same name runs unchanged call
   sites.
8. The demand report lists an unresolved head with all its call sites.

Out of scope, by the milestone table:

- objects, `Binder` and `{class}`/`{new}` (M5);
- typed messages, the script call stack, trace and call statistics (M6);
- tasks (M7);
- shapes, `#require` and the one-way rule (M8b);
- the repetition report, owner findings and JSON check output (M10).

## Design

### Evaluation core

- **Evaluating a node.** `Context::eval(const Node&)` returns `Expected<Node>`:
  - a `Command` runs its handler;
  - a `Var` reads its slot;
  - anything else returns itself.

  `ctx.eval(std::string_view)` parses with the existing reader, then evaluates. Both have
  throwing forms and `try_` twins, as everywhere else.
- **Dispatch.** Look up slot 0's symbol, call the handler with the raw command array, and
  return its result. Nothing else. There is no `switch` on `if`, `set` or any other name;
  a test asserts the evaluator source contains none.
- **Object commands.** Evaluating slot 0 when it is a command (§5, "Object commands
  evaluate slot 0") belongs to M5's object dispatch. M4 reports it as "object dispatch
  arrives in M5".

### Handlers and binding

- **`Args`.** A view of a command array plus the Context:
  - `args.raw(i)` returns the raw node;
  - `args.get<T>(i)` evaluates, then converts through `Convert<T>`;
  - `args.size()` and `args.call_site()` give the argument count and the file:line.
- **`Raw<T>`.** Suppresses evaluation for one parameter. `Raw<Node>` holds the raw node and
  offers `eval()` / `try_eval()`. `Raw<Var>` names a variable slot for `set`.
- **Signature deduction.** A `signature_of<F>` trait covers lambdas, function pointers and
  `std::function`-free callables. It builds a shim that, for each parameter:
  - converts it via `Convert<T>`, or keeps it raw for `Raw<T>`;
  - treats `std::optional<T>` as a trailing optional argument;
  - gives `Args` the rest of the arguments.

  The shim calls the callable and converts the return through a new `Convert<T>::to`
  direction, which `ValueList` already half-provides. Errors name the function, the
  argument index and the call site.
- **Overload sets.** Registering a name again with a different signature adds an overload.
  - Resolution is first match in registration order, on runtime node types, with
    Int→Float promotion tried in a second pass.
  - Two overloads that accept exactly the same parameter types are a registration-time
    error.
- **Tiers and status.** Each registration records its tier (`native`, `script`, `macro`),
  its status (`stub`, `provisional`, `deprecated` with a replacement name) and a
  `doc("...")` string. Collisions follow §5:
  - a native registration supersedes a script one, with a notice and a signature
    comparison;
  - a script `{func}` over a native name is an error unless written `{override ...}`.

### Variables and scoping

- **Dynamic scoping by shallow binding.** Each variable has one value cell per Context.
  `{do ($a) ...}` saves the old values, runs the body, and restores them on exit, including
  on error, through an RAII guard over a value stack.
- **Cost.** Evaluating `$a` loads the cell, which is the "pointer dereference" the spec
  asks for. A callee sees and can clobber the caller's variables; the header says this is
  deliberate.
- **C++ access.** `ctx.var("game_time") = 500` writes a variable. `VarRef` is a resolved
  handle to its cell, cached by the caller.

### Script-defined functions (see Q3)

- `{func name ($a $b) body...}` registers a `script`-tier function whose body evaluates to
  its last expression. Arguments bind by dynamic scoping, like `do`.
- `{override name ...}` redefines a native function and logs that it did.
- Status comes from data: `(status stub)`.

### Generated reference

`moira::write_reference(ctx, path)` lists, sorted by name, every function with:

- each overload's parameter types and arity;
- optional and raw markers;
- tier, status and doc string.

Global variables are listed too. A test regenerates the reference into memory and compares
it with the checked-in `docs/script-reference.md`. A test-only switch rewrites the file,
so updating it is one command.

### `moira::check`

`moira::check(const Array&, const Context&)` walks without evaluating.

- **Errors.** For every command whose head resolves to a registered function, it reports a
  wrong arity for every overload, and a literal of the wrong type in a fixed slot. It skips:
  - unregistered heads;
  - command and variable arguments;
  - `Args` slots;
  - `Raw` slots.

  Every error is located.
- **Demand report.** Unresolved heads and calls to `stub` functions, each with every call
  site, aggregated across all calls to `check` on one `CheckReport`. Informational only.
- **Tests.** `check` runs in CI over `tests/corpus/scripts.dta`, with the heads it uses
  registered by arity (D3: the Harmonix files are never in CI).

### Built-in vocabulary (see Q4)

`if`, `set`, `do`, `func` and `override` are registered by `Context`'s constructor, through
the same `func` API as user code. Nothing else is special.

## Budget

| Area | Estimated lines |
|---|---:|
| Context, cells, `do`/`set`, `VarRef` | 250 |
| Evaluator, `Args`, `Raw<T>`, `eval` text | 200 |
| Signature deduction, shims, `Convert<T>::to` | 400 |
| Overloads, tiers, status, collisions | 150 |
| Script `{func}`/`{override}` | 150 |
| Core vocabulary (control + Q4 choice) | 100–250 |
| `write_reference` | 150 |
| `check` + demand report | 200 |
| **Total** | **~1,600–1,750** |

The core would go from 4,759 to about 6,400–6,500 of the 8,000. That leaves about 1,500
lines for M5 (objects), M6 (messages, stack, stats), M7 (tasks), M8 (binary cache) and
M10's core instrumentation. On rough estimates for those (600 + 400 + 400 + 400 + some),
the budget runs out around M8. I will flag this again at each milestone, per §0, rather
than absorb it; nothing needs deciding now.

## Open questions

Q1–Q4 change the design and go to the project owner. The rest have a clear default noted
here.

**Q1. Where do variable cells and resolved handlers live?**
- **Spec / D5.** Rewrite a command's slot 0 to a `Func` node on first run, with a
  generation in the node's spare bits. Look variables up by name.
- **Recommended.** Give every symbol a dense 32-bit index, stored in `SymbolEntry`'s
  existing padding at no size cost. Each Context keeps flat tables indexed by it: one for
  handlers, one for variable cells.
  - Dispatch is two loads and a bounds check, with no generation check.
  - Variable evaluation is a dereference.
  - Nothing mutates the tree, so D5's thread-safety and macro-sharing caveats disappear.
  - Two Contexts can run the same tree. Under D5, the second would keep re-resolving the
    first one's cached handler.
  - `diff`/`layer_effect` never see a `Func` where a `Symbol` was.
  - Cost: memory proportional to the symbol count per Context, which is a few hundred KB
    at 50k symbols.

  This supersedes D5. The benchmark records both lookups.

**Q2. Do the data layer's typed reads evaluate?**
- **Recommended (per spec §5).** `find_int` and friends evaluate a `Command` or `Var`
  value. `moira::data` gains one evaluator hook, installed by `moira::script`. Evaluation
  uses the calling thread's current Context, set by a scope guard or defaulting to the
  default Context. Without the script layer linked, the current clean error stays.
- **Narrower.** Only script-side accessors (`args.get<T>`, `Raw::eval`, `ctx.eval`)
  evaluate, and data-layer reads keep refusing commands.

**Q3. Script-defined `{func}` in M4?** Done-when item 7 needs a script stub.
- **Recommended.** Bring `{func}`, `{override}` and `(status ...)` into M4, leaving
  `{class}`/`{new}` in M5.
- **Alternative.** Fake the stub with a C++ registration marked `script` tier.

**Q4. Built-in vocabulary.**
- **Recommended.** Context registers control (`if`, `set`, `do`, `func`, `override`) and a
  small calculator core:
  - arithmetic `+ - * / %`;
  - comparison `== != < <= > >=`;
  - logic `and or not`.

  The calculator example uses them; about 150 lines.
- **Minimal.** Control only; the calculator example registers its own arithmetic.
- **Broader.** A Harmonix-like standard library with strings, arrays and `foreach`.

**Defaults, no answer needed.**
- **Handler errors.**
  - A handler may return `Expected<T>`.
  - With exceptions enabled, a thrown `moira::Exception` is caught at the `eval` boundary
    and returned by the `try_` forms.
  - Under `MOIRA_NO_EXCEPTIONS`, `Expected` is the only path.
- **Threading.** A Context is single-threaded. Separate Contexts can evaluate the same tree
  concurrently; this depends on the Q1 recommendation.
- **The `check` Config flag.** `check` is a free function in `moira::script`. The spec's
  "opt-in via a Config flag" waits for `#require` and shapes (M8b), where `check` runs at
  load; until then, callers run it themselves.
