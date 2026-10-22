# Defines a Host Supplies: `ProjectOptions::defines`

A design note, agreed with the owner on Oct 22, 2026, answering hosts whose builds differ by
feature (a server, a demo, a storefront). Results and deviations go into `dev/decisions.md`
when it is built.

## The need

A host knows things a project file cannot: the build's feature tags, the platform, a
command-line flag. It wants a file's `#ifdef` sections to follow them, without a project
file per build. Tools should be able to say which defines were on, and who turned them on.

Today a host can call `cfg.session().define(name)` before or after `open_project`, and
`#ifdef` sees it. But nothing records it: `macro_definitions()` leaves out a define made in
code, so `check`, the reference and `explain` cannot tell a reader that `server` was on.

## The design

- **`ProjectOptions::defines`,** a span of names `open_project` defines beside the project's
  own `(defines ...)`:

  ```cpp
  static constexpr std::string_view kDefines[] = {"server", "steam"};
  open_project(cfg, project, {.defines = kDefines});
  ```

  - Each is recorded with its origin: `host` for these, `project` for the project file's,
    and `file:line` for a file's `#define`.
  - The session lists them all (`active_defines()`), in the order they were made.
- **`Config::set_defines(names)`** changes the host's set after the stack is open: a server
  switching mode, a demo unlocked.
  - It re-reads, in place and as a reload, each layer whose text tested a define that
    changed (`#ifdef`, `#ifndef`), and keeps the rest.
  - The reload's record names the change.
  - A layer's text is not re-read only because its hash is unchanged. The defines it was
    read under are part of what a reload compares.
- **Shown to tools:**
  - `check --baker-json` has `defines`: each active name and its origin. This is a new field
    in a minor version of `baker.check`.
  - The reference JSON has the defines the build ran with, so an editor can grey out the
    sections they rule out.
  - `explain` names, for a value from an `#ifdef` section, the define that chose it.
- **Shipping.** Defines are not a development feature. `set_defines` works in a shipping
  build, since a demo or a server mode is decided there too.

## Cost

- **At open:** one define per name.
- **`set_defines`:** a reload of the layers that tested a changed define. A layer records
  the names it tested while it was read, so this costs nothing until it is called.
- **Nothing on a read or a frame.**

## Tests to write

- A host define is seen by `#ifdef`, and listed with origin `host`.
- `set_defines` re-reads exactly the layers that tested the define, and the value changes.
  A layer that never tested it is kept, the same tree.
- `check`'s JSON and the reference list the defines and their origins.
- `explain` names the define behind a value.
- In a shipping build, `set_defines` still re-reads.
