# Reload off the game thread: design note

*For the six-part TD review's finding 9 (`dev/td-review-2026-11.md`). The owner chose:
read and parse off the game thread, and the game thread only swaps the result, when the
host chooses. Polling uses the stamp check, not `fopen`.*

## The problem

`Config::reload()` does all of its work on the caller's thread, which in `host::Live` is
the game's, inside `begin_frame()`:

| stage | what it does | 122 KB edit | 4.2 MB edit |
|---|---|---:|---:|
| scan | reads every file the stack was built from, whole, and hashes it | | |
| read | reads the changed layer's files and those above it | | |
| parse | parses them, each with the macros of the layers below | | |
| merge, bind, notify | builds the merged tree, binds tweaks, calls subscribers | | |
| all | | 2.3 ms | 29 ms |

The first three are I/O and parsing that touch no state the game reads. Only the last
stage changes what the game sees.

## What exists already

`Impl::reread(Vec<LayerState>& ls, size_t from, ...)` is `const`: it re-reads a *copy* of
the stack from `from` up, all or nothing, into new arrays and sessions, and touches nothing
the Config owns. `reload()` already uses it that way for a directory layer (`listed`). The
scan (`changed()`) reads through the session's reader into a scratch `Str` the Impl keeps,
which is the only mutable state on that path.

## The shape

A reload becomes three calls. The middle one runs anywhere.

```cpp
// The game thread: what to check, and a copy of the stack to check it against.
ReloadJob job = cfg.begin_reload(request);
// Any thread: the scan (by stamp; see below), then read and parse what changed, into the
// copy. Touches nothing the Config owns; the job holds its own scratch.
job.run();
// The game thread, when the host chooses: merge, bind, notify. Refused, and nothing
// changed, if the stack changed since begin_reload (a push, a toggle, another reload).
Expected<bool> changed = cfg.finish_reload(std::move(job));
```

- `reload()` stays, as the three in a row on one thread, for tools and tests.
- `ReloadJob` holds:
  - the layer copies, which share their trees with the Config, so it needs Baker's
    atomic reference counts (the default; D20), as a `LayerChecker` does;
  - the reader and lister it was given;
  - the stack's generation at `begin_reload`;
  - the `ReloadRecord` being filled.
- A live edit during the job is fine: edits live in scratch, which the job does not touch,
  and the merge in `finish_reload` reads scratch as it stands.
- Shipping: `begin_reload` returns an empty job, and `finish_reload` returns false, as
  `reload()` does now.

## The scan, by stamp

`changed()` reads every file whole and hashes it to learn that nothing changed. A file's
stamp (its size and write time, which `Watcher` already reads) answers that for an
unchanged file without opening it. Only a file whose stamp moved is read and hashed, so a
save that leaves the text as it was still makes no merge.

The stamp comes from a hook, as `Watcher::set_stamp_hook` does now, because Baker's core
touches no OS file API but `fopen`. Without a hook, the scan reads and hashes as today.

## In `host::Live`

When the watcher reports a change, `begin_frame()` begins a job and hands `run` to the
host's worker. A later `begin_frame()` finishes the job once `run` has returned. The swap
lands at the frame boundary where reloads land now. A host that gives no worker runs the
job inline, as today.

## The owner's answers

1. **The job runs on the host's worker:** `LiveOptions::run_async`, a callback the game's
   job system runs. Baker starts no thread; without the callback the job runs inline.
2. **A stack that changed under the job is refused:** `finish_reload` changes nothing and
   returns false, and `Live` begins a fresh job at the next frame.
3. **The stamp hook is in `LoadSession`,** beside its reader, so any host's reload scan
   uses it.

## Measuring

Measure before and after with the config review's driver: the game thread's time per
reload, 122 KB and 4.2 MB, and the worker's; the quiet poll's allocations and syscalls per
file. Record them in the D-entry.
