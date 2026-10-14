# Stuntline

Stunt races on tracks laid from a kit of pieces (`docs/background/game-stacks.md` §4.5). The
question it exists to answer: is the track fun to drive, fast, readable, with jumps that land
and moments of airtime, built quickly from a kit?

```sh
build-stacks/bin/stacks_stuntline                          # drive the first track
build-stacks/bin/stacks_stuntline sim --seeds 1-5          # each track: valid, finishes, crashes, airtime, jumps
build-stacks/bin/stacks_stuntline eval "{track.load switchback}" "{track.report}"
```

**Driving.** A pad: right trigger throttle, left trigger brake (and reverse when stopped),
left stick steer, A handbrake. Keys: W or up, S or down, A and D or left and right, space
for the handbrake. R puts the car back on the road; Tab swaps you for the pro driver and back;
N changes track. F1 shows the panels and the Stuntline window: track, driver, and every AI
driver's run over the track.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `race.hpp`, `race.cpp` | L2: the kit and tracks from data, the layout, the car on Jolt's wheeled vehicle, AI drivers, crashes, airtime, jump margins | Engineers |
| `main.cpp` | L2: input, chase camera, script words (`track.`), the window, drawing | Engineers |
| `packs/game/stuntline_core.dta` | L2: the shapes of a piece, a track, a driver (all `(keyed)`) | By pull request |
| `data/default.dta` | L4: the car, the kit, the tracks, the drivers | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/tracks.dta` | Scenarios: each track judged; the pro driving on the game's own steps | The agent |
| `metrics/track.dta` | Objectives: valid, the pro finishes and seldom crashes, airtime in a band, skill spread, jumps that need commitment | By pull request |

## Design

- **A track is laid end to end.** Each piece moves a cursor (position and heading): straights
  and ramps are one box of road; a gap advances with no road; a turn is chords of six degrees
  or less. The racing line is sampled every metre with the speed a corner allows at the car's
  grip. A piece's `in` must match the `out` before it, or the track is not valid.
- **The car is Jolt's wheeled vehicle**, set up as Jolt's own vehicle sample is, with its
  numbers in `(car ...)`. A race is one Jolt world, the road then the car, stepped at 60 Hz,
  and replays bit for bit (a test pins the hash).
- **Crashes**: below the road by five metres, on its back for a second, or stuck for four.
  The car is put back, still, five metres into the last flat piece, so a jump gets its
  run-up again. Airtime is time with no wheel touching.
- **AI drivers** (`(drivers ...)`) steer toward the line `look_ahead` metres on, with
  `noise` in the steering, and hold `pace` times the slowest corner speed within braking
  distance, up to `top_speed`.
- **The analyses** run headless races, once per driver and seed: `{track.finish_time pro}`,
  `{track.crash_rate pro}` (crashes per piece), `{track.airtime_share pro}`,
  `{track.skill_spread}` (rookie over ace), and `{track.jump_commitment}`: every gap is
  cleared at the speed the pro arrives with (a ballistic estimate back to the lip's height,
  at least 0.85 of the gap, since the landing falls away) and would not be at 70% of it.

## Deviations from §4.5

- **No loops or banked turns yet.** Banked chords meet at a ridge where the bank changes,
  which launches the car; banking needs a road surface built as one mesh, not boxes.
- **Sprints, not circuits**: a track runs from start to line; "circuits close" waits for a
  circuit.
- **No ghost replay** of the best lap yet, and no layout grammar to generate tracks.

## Tuning, as the objectives found it

The first canyon jump (a 12 m gap) left the rookie crashing at the landing forever, and the
switchback's 12 m gap cleared even at 70% of the pro's speed: both failed their objectives on
the first run. The canyon's gap is 8 m now, and the switchback's 18 m; over seeds 1–30, 87 of
90 runs pass, the three left being rookie crashes at the canyon landing (`skill_spread`).

## Measured

See `docs/decisions.md` D111.
