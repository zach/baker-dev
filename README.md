# Moira

A small, MIT-licensed C++20 data and scripting library in the spirit of Harmonix's
DataArray, as described in Eric Malafeew's GDC 2005 talk *Data-Driven Programming Made
Easy*.

The goal: replacing a hardcoded constant with a configured, hot-reloadable,
designer-editable value should cost one line and no ceremony.

## Status

**M0 — repository skeleton.** There is no usable API yet. This README describes only
what exists; see `docs/spec.md` for what is planned and `docs/decisions.md` for where
the implementation deviates from the spec.

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Targets: `moira::data` (data layer, usable alone), `moira::script` (script layer,
depends on data), `moira::moira` (both). Build the data layer by itself with
`-DMOIRA_BUILD_SCRIPT=OFF`.

| Option | Default | Effect |
|--------|---------|--------|
| `MOIRA_BUILD_SCRIPT` | `ON` | Build `moira::script` |
| `MOIRA_BUILD_TESTS` | `ON` when top-level | Build the test suite |
| `MOIRA_ATOMIC_REFCOUNT` | `OFF` | Atomic refcounts for multithreaded loading |
| `MOIRA_SHIPPING` | `OFF` | Freeze config; compile out dev instrumentation |
| `MOIRA_NO_EXCEPTIONS` | `OFF` | Non-throwing APIs only |
| `MOIRA_SANITIZE` | empty | e.g. `address,undefined` |

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
