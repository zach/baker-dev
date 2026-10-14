# Wayfarer

Exploration missions in a free-roaming world (`docs/background/game-stacks.md` §4.4). The
question it exists to answer: does exploring lead somewhere? Are missions discoverable,
reachable, paced, and impossible to break?

```sh
build-stacks/bin/stacks_wayfarer                                  # play
build-stacks/bin/stacks_wayfarer sim --seeds 1-5                  # the proof, three bots, two played routes
build-stacks/bin/stacks_wayfarer eval "{world.prove}" "{world.proof_report}"
build-stacks/bin/stacks_wayfarer eval "{world.pacing curious 1800}" "{world.pacing_report}"
```

**Playing.** WASD moves, relative to the camera; Q and E, or the mouse with the right button
held, turn it. Space jumps; held in the air, it glides (once you have the glider). Push into
a brown (climbable) face to climb it (once you have the gloves). Points of interest are
posts: gold when an open objective points at one, dim until found. The Wayfarer window is
the workbench: the map (cells, blocks, glides in blue, drops in yellow, each mission's
objectives linked, the player in red), the gating view (each mission done, open, blocked
from here, or locked), and buttons for the proof and the bots. In the console:
`{mission.start sky_garden}`, `{mission.skip_to lighthouse_signal light}`,
`{world.teleport lighthouse}`, `{world.give glider}`, `{world.autopilot eyrie}`.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `world.hpp`, `world.cpp` | L2: the world from data, navigation geometry, progress, the proof, the bots | Engineers |
| `main.cpp` | L2: the character on Jolt, streaming, the director, the map, script words (`mission.`, `world.`) | Engineers |
| `packs/game/wayfarer_core.dta` | L2: the shapes of a cell, a point, a mission, an objective (all `(keyed)`) | By pull request |
| `data/default.dta` | L4: traversal limits, the cells, the points of interest | By pull request |
| `data/regions/*.dta` | L4: each region's missions | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/world.dta` | Scenarios: the proof, a bot of each style, the climb and the glide played | The agent |
| `metrics/world.dta` | Objectives: no softlocks, everything reachable, readable goals, glide margins, no dead zones, missions on time | By pull request |

## Design

- **Declarative objectives** (Halo 3's): a mission says what must become true. The director
  evaluates each open objective's goal command in script; the natives are `at_poi`, `flag`,
  `has_item`, `mission.done`, and `time_of_day`. A point of interest gives items and sets a
  flag when visited, if the player holds what it needs.
- **The proof reads the same goals as facts** and searches every save point: a place (the
  spawn or a point of interest) and what is held, set, and done, moving between places by
  paths on the navigation mesh with the abilities the items give. A save point from which
  some mission can no longer be finished is a softlock; an objective done in no save point
  is unreachable; a point no save point is at is never reached; a goal the proof cannot
  read (`{> {time_of_day} 20}`) is reported, not guessed.
- **Abilities are navigation flags.** Climbable faces up to `wayfarer.climb_max` get climb
  links (both ways); a glide in the data becomes a glide link only if the glide ratio
  (`glide_speed` over `glide_sink`) makes it; drops are one-way walks. So "reachable with
  the abilities they can have by then" is a path query, and tuning a limit changes what the
  proof finds.
- **Bots** walk the navigation mesh at the run speed, finding what comes within
  `wayfarer.discover_radius`: direct follows the open objectives, curious heads for what it
  can see, completionist visits everything nearest first. Each fetches a tool (an item it
  does not hold) when no objective can be reached. The pacing measure is the longest
  stretch without a discovery, up to the last find once everything is found.
- **The character is Jolt's CharacterVirtual**: a capsule that walks and steps, climbs a
  climbable face it pushes into (for `climb_max / climb_speed` seconds, then a push over the
  top), and glides when space is held in the air. The played scenarios walk the proof's
  path with it (`{world.autopilot}`), so the runner and the game agree on the climb and the
  glide.
- **Streamed by cell.** The cells within `wayfarer.stream_radius` of the player's are built
  (static bodies and a navigation tile each), and dropped a cell further out; a reload
  rebuilds only the cells whose source changed. The missions load with their region files
  in the layers: the director keeps the whole graph, which is small (§4.4, "On PC"). The
  analyses build every cell's tile once per load.

## Not yet

Terrain other than boxes; swimming and mounts; the director's hint is a line of text, not
a marker; a time-of-day sky; bots that use the Jolt character rather than the mesh (the
played scenarios cover two routes); streaming a cell's navigation tile off the main thread
(about 16 ms each, Release).

## Measured

See `docs/decisions.md` D112.
