# Tooling layers: what Baker gives tools, what the host does, what lives outside

*A design note (D184). The owner asked where the line falls between development tools in
Baker, tooling outside Baker, and the stack (the game) itself, and whether there are three
clear layers. There were, in practice. The rules lived in D156, `INTEGRATIONS.md`,
`stacks/agent.md` and the integration plans, and each requirement note was placed by
judgment. This note writes the layers down, with a test for each, so the next note is
placed by rule.*

## 1. The three layers

| | Layer | What it holds | What it never does |
|---|---|---|---|
| 1 | **Baker** (the library) | What needs Baker's meaning of the data | Touch the operating system; decide policy; draw UI |
| 2 | **The host** (the game, or any program linking Baker) | The operating system and policy, and the game's own vocabulary | Reimplement Baker's rules |
| 3 | **External tools** (editor, agent bridge, live-ops service, engine plugin) | A user interface, a protocol, another product's terms | Link the game's code, or reimplement Baker's rules |

### Layer 1: Baker

- **Belongs here:** whatever two hosts would otherwise each reimplement from Baker's rules.
  - Provenance and `explain`; `check` and its report; layer policies; write-back;
    the reference, check and reply JSON documents.
  - The project file's format, with `read_project`, `open_project` and `find_project`.
  - `declare`, `macro_definitions`, `script_functions`, `stack_files`.
- **The I/O rule.** Baker reads through the host's reader and writes through its writer.
  Time comes from the host's time source. Baker opens no socket, starts no watcher thread
  and draws nothing.
- **The surface is data, not a UI.** Functions return values, `List`s and reports. Writers
  produce JSON or Baker text. Hooks take callbacks. `bind_config_tools` and `eval_request`
  turn that data into script functions and one-line replies, so any transport can carry
  them.
- **Pay for what you call.** A host that calls none of the tool surface pays code size in a
  static library and nothing at run time. A new tool feature adds nothing to the per-frame
  or per-call path of a game that does not use it, and that is measured (D178, D180).

### Layer 2: the host

- **Belongs here:** whatever needs the operating system, the game's own code, or a policy
  decision.
  - **The operating system:** watching files, the console's socket, windows and panels,
    processes, the command line.
  - **The game's code:** commands that need the game's own script functions and types
    (`check`, `reference --json`, `sim`), because only the game binary has them
    (`stacks/agent.md`).
  - **Policy:** when to reload; which tools to bind; whether the console may write files
    (`--console-write`, D182); which layers a simulation leaves out.
- **The stacks' `base` is the reference host.** It shows one way to do each of these.
  It is not a library, and nothing in Baker depends on it.

### Layer 3: external tools

- **Belongs here:** a separate process with its own UI, protocol or terms. Each is in its
  own repository (D153, `INTEGRATIONS.md`).
- **The contract.** An external tool talks to a game only through what Baker and the host
  publish:
  - **Files:** the project file, and the build's reference JSON.
  - **Documents:** the reference, check and reply JSON, each versioned.
  - **The console:** one-line requests and replies, on 127.0.0.1.
  - **Commands:** the game binary's own commands.
- **It thinks in the game's terms but never decides them.** When it needs what neither layer
  gives, it writes a requirement note, and the need is met in layer 1 or 2.

## 2. Placing a need

Ask in order; the first yes decides.

1. **Does it need Baker's rules, so that a second host would have to copy them?** Then it
   goes in Baker (layer 1), as data and hooks, behind no new I/O.
   - Examples: `stack_files` (D180), `declare` (D175), whole-number write-back (D183).
2. **Does it need the operating system, the game's code, or a policy choice?** Then it goes
   in the host (layer 2). Baker gives the host what it needs to do it well, which may be a
   layer-1 feature.
   - Examples: the file watcher, which uses `stack_files`; `--console-write` (D182); the
     build step that writes the reference (D177).
3. **Otherwise it is an external tool's own** (layer 3). Examples: hover, completion, an
   MCP tool's shape, a live-ops backend.

A note that seems to need both layer 1 and layer 2 usually splits: the semantic half goes in
Baker, and the host uses it. D180 (`stack_files`) and the watcher that reads it are the
pattern.

## 3. The tiers inside Baker

Baker's own tool surface has three tiers, set by the build and by the host's calls.

| Tier | What | Config tools that bind (`bind_config_tools`) |
|---|---|---|
| **Always**, shipping builds included | Reading, provenance, `explain`, `check`, the JSON documents, the project file, `stack_files`, `declare`, `script_functions`, received layers under a policy | `config.get`, `config.source`, `config.explain`, `config.layers`, `config.effect`, `config.diff` |
| **Development builds** (compiled out under `BAKER_SHIPPING`) | Live edits (`set`, `Tweak::set`), reload, layer toggles, scratch, the journal, edit-to-see stats, snapshots, edit mode and its source maps | also `config.set`, `config.toggle`, `config.reload`, `config.unsaved`, `config.journal`, `config.loop`, `config.snapshot`, `config.restore` |
| **On request** (`{.allow_write = true}`), development builds only | Writing files | also `config.write_back`, `config.promote` |

- **The tool surface is opt-in.** `bind_config_tools`, `eval_request` and the trace hook
  do nothing until a host calls them.
- **A frozen Config refuses edits even in development** unless `live_layers` lets the
  host's live-ops overlays through.

## 4. A host kit

- **The problem.** Three things in the stacks' `base` are not specific to any game, and
  every host rewrites them:
  - the file watcher (inotify and ReadDirectoryChangesW);
  - the console server, a socket that feeds `eval_request`;
  - the tool commands (`check`, `reference --json`, `sim`, `diff`, `promote`) and their
    JSON.
  The Godot integration is rewriting the first two, and `examples/remote-console` is a
  second console server.
- **Why they are not layer 1:** they touch the operating system, which the I/O rule keeps
  out of the core.
- **The choices:**
  1. **Leave them in the host.** The stacks and the examples stay as references, and each
     host copies what it needs. This is cheapest, but the copies drift.
  2. **A host kit:** an optional, separate library beside the core, MIT and in this
     repository. It would hold a watcher over `stack_files()`, a loopback console server
     over `eval_request`, and the tool commands' shared code.
     - The core stays free of I/O, and a host links the kit or not.
     - The kit would ship with releases like `baker::shape`, outside the core.
     - The stacks' `base` and `examples/remote-console` would use it.
  3. **The commands only:** move the tool commands' shared code (argument parsing, the
     JSON they print) into the kit, and leave sockets and watchers to hosts.
- **Decided: option 2** (D186). `baker::host` is planned in `dev/host-kit-plan.md`, with
  the watcher, the console, the live loop, and the commands. Under `BAKER_SHIPPING` the
  watcher and the console are inert.

## 5. Writing it for users

`docs/tools.md` gives users of the library the user-facing half:
- what Baker gives tools (tier by tier);
- what it leaves to the host (watching, transport, commands, policy);
- how an external tool should talk to a game.

It carries no process and no stacks.
