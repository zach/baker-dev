# Baker Among the Engines: a Competitive and Integration Landscape

What larger engines, libraries, services and tools do in Baker's territory, what none of
them do, and which integrations would most advance Baker's mission. The plans for the
integrations chosen here are in `dev/integration-plans.md`.

Oct 15, 2026. A research note. The owner's choices on it are D153.

Companion to `iteration-backgrounder.md` (Unreal, Unity and Frostbite on iteration; the
use cases), `content-updates.md` (patches, DLC and modding), `game-stacks.md` (the agent
harness) and `harness-tree-vision.md` (the four properties of a harness).

---

## 0. Method and limits

- **Sources.** Public documentation, public talks, patch notes and press only, gathered
  by web search in October 2026. The sources are listed at the end, by section. Items
  marked *(unverified)* rest on secondary reporting or general knowledge.
- **Facts, not expression** (`INTEGRATIONS.md`, rule 4). This note records how other
  systems behave as seen from outside. No code, header or identifier from any of them
  was read or copied. The plans that follow from it design Baker's side in Baker's terms.
- **What "integration" means here.** Something outside Baker that Baker is paired with:
  an engine Baker runs inside, a service that feeds it, or a tool that reads its data.
  Each lives in its own repository and depends on Baker, never the reverse. When a pairing
  needs something Baker lacks, that need comes back as a generic Baker feature.
- **The yardstick is the mission:**
  - edit to see, in the running game, fast;
  - for designers, programmers and agents alike;
  - with the values found by playing reaching version control;
  - and live changes after ship made safely.

  Reach and novelty count only through those.

---

## 1. Baker's pieces, as a comparison grid

Each system below is scored against the same nine columns:

| Column | What Baker has |
|---|---|
| **Tweak** | `Tweak<T>`: a named, self-registering, typed constant with metadata, read in one load |
| **Layers** | Ordered named layers, toggled, placed, removed at runtime; `#remove` markers |
| **Provenance** | `source()`: layer, file, line, and generating tool for every value |
| **Write-back** | A value tuned live goes back to its source token as a one-line diff; `promote` |
| **Schema** | Shapes checked at load, located; a softer pack may narrow, never loosen |
| **Script** | Lazy commands, native binding, objects, messages, coroutine tasks |
| **Reload** | Host-triggered, hash-based reload; an unchanged poll costs nothing |
| **Live ops** | `freeze` plus `live_layers`; overlays rolled back in one call |
| **Instrumented** | Edit-to-see telemetry, the edit journal, `check` as versioned JSON, the remote console |

---

## 2. Engines

### 2.1 The grid

| Engine | Tweak | Layers | Provenance | Write-back | Schema | Script | Reload | Live ops | Agent / remote hook | License |
|---|---|---|---|---|---|---|---|---|---|---|
| Unreal Engine 5 | Console variables | ini hierarchy; per-write priority tiers | tier only | whole section to an ini | struct rows, on import | Blueprints, Verse | Live Coding; reimport | hotfix of ini and tables | Remote Control (HTTP, WebSocket) | commercial EULA, royalty |
| Unity 6 | none | none locally | none | ScriptableObject edits persist, whole asset | C# types | C# | domain reload options | Remote Config, Game Overrides | none official | commercial, seats |
| Godot 4 | ProjectSettings | feature-tag variants, `override.cfg` | none | editor saves whole `.tres` | typed exports | GDScript | script and scene reload | none | remote scene tree, debugger plugins | MIT |
| O3DE | console variables | Settings Registry: ordered JSON merge and patch | none | none | reflected JSON | Lua, Script Canvas | Asset Processor | none | `--regset`, registry console | Apache-2.0 / MIT |
| Bevy | none | none | none | whole scene file | reflection | Rust | asset watcher | none | Bevy Remote Protocol (JSON-RPC) | MIT / Apache |
| Defold | none | `game.project` only | none | none | protobuf-defined | Lua | editor hot reload, to devices too | content updates | editor HTTP (undocumented) | Defold License *(Apache-based, restricted; unverified)* |
| Source / Source 2 | console variables | cfg exec chain | none | archived values written to `config.cfg` | KeyValues | VScript | partial | none | console, rcon | proprietary |
| Flax, Stride | none | none | none | none | C# / C++ types | C# | code hot reload | none | none | source-available royalty / MIT |
| raylib, sokol, SDL custom | none | none | none | none | none | none | none | none | none | zlib and similar |

### 2.2 Notes

- **Unreal** comes closest on tunables:
  - Its console variables are named, typed, self-registering values with help text and
    a shipping lockout.
  - Each write carries a priority tier, and a lower-tier write is dropped. A variable
    can name the tier that last set it, but not a file or a line.
  - "Set as Default" and `SaveConfig` write whole sections, not a one-line diff.
  - DataTables round-trip through CSV or JSON only as a full re-import; a third party
    sells a three-way merge tool for exactly this.
  - Remote Control exposes properties and functions over HTTP and WebSocket, and the
    community's MCP servers sit on it.
  - Hotfixes download ini and table files from title storage and keep backups so they
    can be undone.
  - Share: about 28% of 2024 Steam releases and 31% of revenue.
- **Unity** has no tunable system.
  - ScriptableObjects persist play-mode edits only because they are the asset: the whole
    YAML file is rewritten, with no provenance.
  - Remote Config is a separate, remote-only key-value system with audience overrides.
  - Share: about 51% of 2024 releases and 26% of revenue.
- **Godot**:
  - Edits made in the remote scene tree of a running game are discarded when it stops.
    That gap is Baker's pitch exactly: the values found by playing are lost.
  - Feature tags and `override.cfg` give at most three layers, with no provenance.
  - GDExtension is a C ABI, so no engine rebuild is needed. The editor and debugger
    plugins give a channel between the running game and the editor.
  - Share: about 5% of 2024 Steam releases and growing fastest.
- **O3DE's Settings Registry** is the closest public analog to Baker's Config. It merges
  ordered JSON files with merge-patch and JSON Patch, specialized by tags in file names.
  - Merge-patch cannot delete array elements or set null; Baker's `#remove` can.
  - It has no provenance or write-back, and its community is small.
- **Bevy's remote protocol** (JSON-RPC over HTTP, with community MCP wrappers) is the
  most agent-ready engine interface. It exposes components, not tunables with history.
- **Custom engines on raylib, sokol or SDL** have nothing in this space. They take about
  13% of Steam releases. Baker drops in as a static library and needs no plugin. The
  stacks (`stacks/`) are already this kind of host.

### 2.3 In-house systems, from public talks

Most of these are covered in `iteration-backgrounder.md` and `halo-tags-vs-dataarray.md`.

| System | What is public | Bearing on Baker |
|---|---|---|
| Naughty Dog's DC | Racket-based data compiler with many DSLs | Lisp data with macros, compiled offline. Baker's layers and write-back act at runtime. |
| id Tech decls | Text declarations with `reloadDecls` and a browser | Text plus hot reload in 2004, with no layers or provenance |
| Bungie's Halo tags | Hot-loaded dev build, monolithic ship build | The same dev/ship split as Baker's text and cache |
| Frostbite | "A Tale of Three Data Schemas": runtime, disk and authoring schemas | Argues for keeping schema versions explicit (packs' versions, D147) |
| Remedy Northlight | Live-edit pipeline from the editor into the running game (GDC 2024) | The same loop, for world entities rather than values |
| Riot micropatches | Server-side overrides of scripts and data, whole-file per property set | Baker overrides by key path, with provenance and removal |

### 2.4 What the engines say

- **Every engine has part of Baker's model, and none has write-back.** The nearest are
  Unity's accidental persistence and Unreal's whole-section save. Studios rebuild pieces
  of the model in-house over and over: DC, decls, micropatches, hotfix backups.
- **The local stack and the live stack are separate systems everywhere.** Examples: ini
  and hotfix; `override.cfg` and nothing; ScriptableObjects and Remote Config. In Baker
  they are one stack with one provenance.
- **Agent interfaces are converging on JSON-RPC and MCP over engine objects.** Examples:
  Remote Control, Bevy's remote protocol, and the Godot, Unity, Unreal and Blender MCP
  servers. None of them can say where a value came from, or turn an agent's change into
  a reviewable diff.

---

## 3. Libraries and languages in custom stacks

### 3.1 Embedded scripting

| Language | License | Reload | Relation to Baker |
|---|---|---|---|
| Lua, LuaJIT | MIT | by hand (re-run chunks) | Complement for heavy gameplay logic. Its values live in the VM, with no provenance. |
| Luau | MIT | host-driven; types, LSP | The best-typed Lua; a bridge would expose tweaks as read-only values |
| AngelScript, Squirrel, Wren, mruby, QuickJS | zlib / MIT | manual, or module rebuild | Complements |
| Janet, s7 | MIT / BSD | REPL-driven | Lisp-family peers; a benchmark for console ergonomics, not an integration |
| Daslang | BSD-3 *(unverified)* | a live host keeps windows, GPU state and marked globals across reloads | The strongest code-reload story. It keeps state alive; it does not send values back to source. |

Verdict: complements. Baker's commands are DataArray's lazy commands, not a general VM, and
no VM writes a value back to its source.

### 3.2 Configuration and schema languages

| System | Layering | Provenance | Write-back | Validation |
|---|---|---|---|---|
| CUE | unification (values must agree) | positions on errors | format only | constraints are values; strong |
| Pkl, Jsonnet, Dhall | amend/extend, `+:`, record merge | good errors | none (generators) | typed (Pkl, Dhall) |
| Nickel | merge priorities (`default`, `force`, numeric) | contract blame | none | contracts |
| TOML (toml_edit), YAML (ruamel) | none | spans | format-preserving editing | external |
| Protobuf, FlatBuffers, Cap'n Proto | field masks or none | none | text form only | schema evolution, codegen |
| JSON Schema | `allOf` | JSON Pointer | none | the de facto validator; drives editor completion |

Verdict: these are build-time generators or service config. None has a running game,
layers that change at runtime, or edits that flow back. Two ideas are worth studying as
facts: Nickel's priority vocabulary, for explaining shadowing, and format-preserving
editors, as a check on write-back's claims.

### 3.3 Engine-side libraries

| Library | License | Overlap | What pairing with Baker unlocks |
|---|---|---|---|
| flecs | MIT | Reflection, a scripting DSL, a REST API with a web explorer that edits components | The closest library peer. Explorer edits are lost on restart, so Baker could supply write-back and layers. The stacks have no ECS by design (`game-stacks.md` §2.1). |
| EnTT | MIT | Reflection | Shapes into meta registration |
| Tracy | BSD-3 | Plots, messages, app info | Reload events and edit-to-see times on the profiler timeline, so a frame-time change lines up with the value change that caused it |
| netImgui, ImPlot | MIT | Remote ImGui; plots | The tuning harness on a device, drawn on a PC; plots of tweak history |
| SDL3 storage, PhysFS | zlib | Title/user storage; archive mounts | Readers and writers for Baker's existing hooks |
| cr.h, Live++ | MIT / commercial | C++ code reload | Code reload and data reload together cover the whole loop. Live++ only through a separate repository. |
| Jolt, RmlUi | MIT | Settings structs; data-bound UI | Samples in the stacks |

---

## 4. Live ops and backends

### 4.1 Remote config and feature flags

| Service | Model | Delivery to C++ | Rollback and safety | Experiments |
|---|---|---|---|---|
| Firebase Remote Config | ordered conditions, percentile buckets | fetch then activate; real-time only on Android and Apple | template versions | A/B, rollouts |
| Unity Remote Config | audience overrides, no logic | SDK | dashboard | via overrides |
| PlayFab Title Data | flat keys; overrides through experiments, where an empty value removes a key | API; minutes to propagate | client cache | scorecards |
| AWS AppConfig | profiles and flags | agent or polling | **validate (JSON Schema, Lambda), deploy in steps, bake, roll back on an alarm** | none |
| LaunchDarkly, Statsig, GrowthBook, Flagsmith, Unleash | rules, segments, buckets | SDKs (no GrowthBook C++ SDK found) | audit logs | statistics |
| OpenFeature, flagd | vendor-neutral API; flagd layers several sources, later wins | C++ SDK at v0.0.1, Bazel only | none | none |

What these services have that Baker lacks: audience targeting, percentage bucketing,
experiment statistics, staged rollout with automatic rollback.

What every one of them lacks: provenance, typed nested data validated by a schema,
ordered named layers, removal (PlayFab aside), and any path for a value back into version
control. Tuning that lives in a vendor console has no diff and no review.

Vendor churn is a reason for Baker to own the format and keep transports pluggable:
- Statsig was acquired.
- Unity retired Remote Settings.
- Hathora shut down in May 2026, and a shipped game lost its multiplayer.

### 4.2 Backends

- **Overlay sources.** Nakama storage (Apache-2.0), Epic Online Services title storage,
  PlayFab title data, Beamable content and SpacetimeDB tables (BSL) can each hold a
  published overlay. The common shape is: fetch a versioned blob, verify it, push it as
  a live layer.
- **Dedicated servers** have to load the same overlay stack as the clients.

### 4.3 What studios' live ops teach

- **Restrict what a hotfix can change.** Hearthstone hotfixes only numbers; anything
  else needs a different kind of patch. Baker's analog is `live_layers`, which so far is
  all or nothing.
- **Hotfixes skip QA**, so validation at push time must be automatic. Riot's
  micropatches went straight to live.
- **A rollback can itself break things** when the data and the schema have diverged:
  Path of Exile 3.7.4, 123 minutes down. The equivalent in Baker would be a pack version
  check on the overlay.
- **Keep undo state**, as Unreal's hotfix ini backups do; Baker's equivalent is
  one-call removal.
- **Say what is live**, to players and to operators. In Baker that is provenance.
- **Overlapping experiments interfere.**

### 4.4 Modding

Paradox, RimWorld, Factorio, KSP Module Manager, Bethesda and Minecraft data packs are
covered in `content-updates.md`. What modders need: deterministic order, element-level
patches, removal, list merging, safety, and above all **"who set this value"**. That last
need is the whole reason xEdit exists, and Baker's provenance already holds the raw
material. Baker has order, keyed patches, removal, reload and text-only sessions. A
built-in **conflict report** would replace a category of third-party tools. It is Baker's
own feature and needs no integration.

---

## 5. Developer tools and agents

- **Editors.** The Language Server Protocol (one server for VS Code, Neovim, Helix, Zed
  and JetBrains), tree-sitter grammars and TextMate grammars are how a small language
  reaches every editor. Racket, Fennel and KDL show the route.
  - `.dta` collides with Stata's data files in editors and on GitHub. A Baker extension
    must activate by workspace or content, not by extension alone.
- **Agents.** MCP servers exist for Unity, Godot, Unreal and Blender, all from the
  community. Their tools follow one pattern: inspect, mutate, run, read the log,
  screenshot. Three weaknesses run through all of them:
  - no provenance;
  - edits that land in binary scenes or arbitrary code, not in a reviewable diff;
  - validation left to the agent reading the console.

  Baker already has versioned `check` JSON, provenance, an agent layer, A/B toggles,
  snapshots, the journal and one-line write-back. The stacks expose them as commands of
  each game binary plus a loopback console (`stacks/agent.md`). `agent.md` itself calls
  that surface a stand-in for a tool server.
- **Designer data.**
  - Pipelines from Google Sheets or Excel are one-way. The values tuned in the build
    drift from the sheet, and no tool sends them back.
  - CastleDB and Depot (MIT, Apache) are typed grids over JSON. LDtk is MIT; the Tiled
    editor is GPL, though its formats can be read freely.
- **Analysis.** nanobind (BSD-3) would put a Config, its provenance and the journal into
  Python for balance notebooks.

---

## 6. Where Baker stands

### 6.1 What nobody ships

No engine, service or library found ships any of these:
1. **One-line write-back** of a value tuned in the running game, to the token it came
   from.
2. **Per-value provenance** to layer, file, line and generating tool.
3. **One stack for local and live layers**, with promote, snapshot, A/B and removal.
4. **Edit-to-see telemetry and a journal** of live edits and their fates.
5. **Located load-time schema errors** in a text format, and the same as versioned JSON
   for tools.

### 6.2 Where Baker is behind

Each item below names a gap the pairings would hit. The plans turn them into Baker
features.

| Gap | Who has it | Hit by |
|---|---|---|
| Errors carry file and line, but no column or span (`error.hpp`) | every LSP-backed language | editor tooling |
| The remote console is an example: text lines and `=> value` replies, no request ids, no structured errors | Bevy's protocol, Unreal Remote Control | agents, editors, engine bridges |
| `reference` is Markdown only; native signatures are not machine-readable | LSP servers, MCP tool schemas | agents, editors |
| No single call gives a path's whole layer chain (each layer's value, line, and whether it is shadowed or removed). `source()` names the winner; `layer_effect` works per layer. | xEdit (for mods), Unreal's priority tiers (winner only) | agents, editors, Godot, modders |
| A layer cannot be checked before it is enabled under a policy narrower than `live_layers`, such as data only, or numbers only under listed paths | AppConfig validators; Hearthstone's numbers-only hotfixes | live ops, agents' proposals |
| Targeting, bucketing and statistics | every flag service | live ops (left to vendors on purpose) |
| A tweak's path must outlive it (`const char*`), so a scripting host builds tweaks awkwardly | — | engine bridges |

---

## 7. The long list, scored

Columns:
- **Mission** is the sum of five yes/no marks: the loop, designers, agents, values
  reaching version control, and safe live ops.
- **Shows** is whether the pairing demonstrates §6.1, rated low, mid or high.
- **Reach** is the audience it opens.
- **Cost** is the rough effort.
- **Friction** covers the license policy and the Acton review (`.claude/skills/gamedev-td`).

| # | Integration | Mission (0–5) | Shows | Reach | Cost | Friction | Verdict |
|---|---|---|---|---|---|---|---|
| 1 | **Agent bridge**: an MCP server over a Baker game, live or headless | 4 (loop, agents, VCS, live ops) | high | every MCP client | low–mid | none | **select** |
| 2 | **Editor tooling**: tree-sitter, an LSP driven by `check`, live values in the editor | 4 (loop, designers, agents, VCS) | high | every editor | mid | none | **select** |
| 3 | **Godot 4** through GDExtension | 4 (loop, designers, agents, VCS) | high | about 5% of releases, growing | mid | none (MIT) | **select** |
| 4 | **Published overlays**: vendor-free HTTPS, then Nakama; bridges to flag vendors later | 3 (agents, VCS, live ops) | high | every live game | mid | low (vendor bridges private) | **select** |
| 5 | Spreadsheet/CSV round-trip with provenance | 3 | mid | most designers | low–mid | none (CSV); Sheets API in its own repo | runner-up |
| 6 | Tracy adapter for reload telemetry | 1 | low | engine programmers | low | none | quick win, any time |
| 7 | Unreal plugin: console variables backed by Baker layers, with file and line | 3 | high | largest professional audience | high | commercial EULA; private repo | later; agents first |
| 8 | Unity native plugin and C# facade | 2 | mid | largest by releases | high | commercial; P/Invoke loses the one-load read | no |
| 9 | O3DE Gem | 2 | mid | small | mid | none | no |
| 10 | flecs bridge | 2 | mid | ECS users | mid | Acton: entanglement | no |
| 11 | Luau/Lua bridge | 1 | low | Lua users | mid | none | no |
| 12 | netImgui and ImPlot in the harness | 2 | low | console and mobile tuning | low | none | fold into the harness |
| 13 | Modding: a conflict report, then mod.io/Workshop packs | 3 | high | modded games | low (report) | platform terms (distribution) | the report falls out of plan 1's `explain` |
| 14 | Python through nanobind | 1 | low | analysts | low | none | later |
| 15 | OpenFeature provider; flagd importer | 1 | low | service people | low | none | wait for the C++ SDK |
| 16 | JSON Schema export of shapes | 2 | low | JSON tools, LLM structured output | low | none | fold into the machine-readable reference |

### Why these four

- **They cover the mission between them:**
  - the agent's loop (1);
  - the designer's and the agent's text editing (2);
  - a real engine's play-to-source loop (3);
  - safe changes after ship (4).
- **Each demonstrates something nobody ships** (§6.1) in a setting where the gap hurts.
- **They share their Baker-side needs.** Plans 1, 2 and 3 need the same four Baker
  features: an `explain` of a path's layer chain, a structured request/reply, a
  machine-readable reference, and column spans on errors. Plan 4 needs a
  candidate-layer check that plan 1's `propose` tool also uses. Building those once
  serves every pairing, and serves hosts that use no integration at all.
- **None of them is restricted.** MCP, LSP, tree-sitter and Godot are open, and so is the
  reference backend, Nakama (Apache-2.0). The commercial vendors in plan 4 come later, in
  private repositories, through a generic key-map adapter.

**Unreal** is the one strong candidate left out. It has the largest professional
audience, and the pitch would be easy to grasp: console variables with file-and-line
provenance and write-back. Against it:
- a commercial EULA, so a separate, private repository;
- a large engine around a small library;
- `iteration-backgrounder.md`'s judgment that Unreal already covers most of the ground.

It is better taken up once plans 1 and 2 have shaped the generic surface it would use.

---

## Sources

### Engines

- Unreal console variable flags and history:
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/EConsoleVariableFlags
  and https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/IConsoleVariable/Unset
- Unreal data-driven gameplay elements:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine
- DataTable merge tool: https://csaf.itch.io/datatable-merge
- Unreal Live Coding:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime
- Unreal Remote Control:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-control-for-unreal-engine
- Unreal hotfix manager:
  https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Hotfix/UOnlineHotfixManager
- Unity domain reload:
  https://docs.unity.com/en-us/engine/6000.6/manual/programming-environment/code-reload-serialization/code-reload/code-reloading-editor/domain-reloading
- Unity ScriptableObject edits persisting after play:
  https://discussions.unity.com/t/prevent-custom-scriptableobject-asset-persisting-changes-after-exiting-play-mode/545928
- Unity Remote Config: https://docs.unity3d.com/Packages/com.unity.remote-config@3.0/manual/GameOverridesAndSettings.html
- Godot ProjectSettings: https://docs.godotengine.org/en/latest/classes/class_projectsettings.html
- Godot debugging tools: https://docs.godotengine.org/en/4.1/tutorials/scripting/debug/overview_of_debugging_tools.html
- Godot 4.4 release: https://godotengine.org/releases/4.4/
- O3DE settings: https://docs.o3de.org/docs/user-guide/settings/
- O3DE console: https://docs.o3de.org/docs/user-guide/settings/az-console/
- Bevy Remote Protocol: https://docs.rs/bevy_remote
- Defold hot reload: https://defold.com/manuals/hot-reload
- Valve ConVar: https://developer.valvesoftware.com/wiki/ConVar
- Engine market shares: https://gamedevreports.substack.com/p/video-game-insights-game-engines
- Naughty Dog: https://con.racket-lang.org/2013/
- Frostbite: https://www.ea.com/frostbite/news/a-tale-of-three-data-schemas
- id Tech decls: https://iddevnet.dhewm3.org/doom3/declfiles.html
- Remedy: https://gdcvault.com/play/1034236/Real-Time-World-Editing-Technology
- Riot: https://technology.riotgames.com/node/126

### Libraries and languages

- Daslang live reload: https://daslang.io/doc/reference/utils/daslang_live.html
- Luau: https://github.com/luau-lang/luau
- Nickel: https://github.com/nickel-lang/nickel
- CUE: https://cuelang.org/docs/
- Pkl: https://pkl-lang.org/blog/introducing-pkl.html
- toml_edit: https://docs.rs/toml_edit
- flecs: https://www.flecs.dev/flecs/md_docs_2FlecsScript.html and https://github.com/flecs-hub/explorer
- Tracy: https://github.com/wolfpld/tracy
- netImgui: https://github.com/sammyfreg/netImgui
- SDL3 storage: https://wiki.libsdl.org/SDL3/CategoryStorage

### Live ops

- Firebase Remote Config, C++: https://firebase.google.com/docs/remote-config/use-config-cpp
- PlayFab title data: https://learn.microsoft.com/gaming/playfab/features/data/titledata
- AWS AppConfig safe deployment:
  https://aws.amazon.com/blogs/aws/safe-deployment-of-application-configuration-settings-with-aws-appconfig
- OpenFeature C++ SDK: https://github.com/open-feature/cpp-sdk
- flagd: https://flagd.dev
- Nakama storage: https://heroiclabs.com/docs/nakama/concepts/storage
- Epic Online Services title storage:
  https://onlineservices.epicgames.com/news/retrieve-game-specific-data-from-the-cloud
- Hathora shutdown: https://docs.edgegap.com/docs/tools-and-integrations/switch-from-hathora
- Hearthstone 28.0.2 patch notes:
  https://us.forums.blizzard.com/en/hearthstone/t/2802-patch-notes/116446
- Path of Exile 3.7.4 incident report:
  https://devtrackers.gg/pathofexile/p/c8b4e8c1-3-7-4-deployment-downtime-incident-report
- A/B testing at King: https://blog.crisp.se/2016/11/22/yassalsundman/ab-testing-at-king
- xEdit and merge patches:
  https://forums.nexusmods.com/topic/4028940-should-i-use-a-merged-patch-a-bashed-patch-or-both
- Minecraft data packs: https://minecraft.wiki/w/Data_pack

### Tools and agents

- Model Context Protocol: https://modelcontextprotocol.io
- Language Server Protocol: https://microsoft.github.io/language-server-protocol/
- tree-sitter: https://tree-sitter.github.io
- GitHub Linguist: https://github.com/github-linguist/linguist
- Godot MCP: https://archestra.ai/mcp-catalog/coding-solo__godot-mcp
- Unity MCP: https://playbooks.com/mcp/coplaydev/unity-mcp
- Blender MCP: https://www.augmentcode.com/mcp/blender-mcp
- nanobind: https://github.com/wjakob/nanobind
- CastleDB: https://lib.haxe.org/p/castle
- Depot: https://kylekukshtel.itch.io/depot
