# Ledger

A tycoon economy (`dev/background/game-stacks.md` §4.2). The question it exists to answer:
does the economy produce interesting decisions from the first minute to the fortieth hour,
with no dominant strategy, no dead ends, and the pace the designer intends?

```sh
build-stacks/bin/stacks_ledger                            # play: build, sell, watch the money
build-stacks/bin/stacks_ledger sim --seeds 1-10           # the bot strategies, judged by metrics/
build-stacks/bin/stacks_ledger eval "{econ.compete}" "{econ.advance_days 720}" "{econ.report}"
python3 -I stacks/ledger/tools/export_economy.py          # economy.xlsx -> data/economy.dta
```

**Playing.** The Ledger window: buy buildings, sell them back at half their cost, choose
which goods you sell, and run the clock at 1x, 4x, or 16x (a day is five seconds at 1x).
Farms and ranches are open from the start; each later building unlocks by what you have
earned. Hover a building for its recipe, a locked one for its unlock. F1 shows Baker's
panels.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `economy.hpp`, `economy.cpp` | L2: goods, recipes, markets, a company's day, the bot strategies | Engineers |
| `main.cpp` | L2: the clock, script words (`econ.`), the Ledger window, the town | Engineers |
| `packs/game/ledger_core.dta` | L2: the shapes of a good, a building, a strategy, a milestone | By pull request |
| `economy.xlsx` | L3: the designers' spreadsheet: the Goods and Buildings sheets | Designers |
| `tools/export_economy.py` | L3: the sheet to `data/economy.dta`, with an `origin` form | Engineers |
| `data/economy.dta` | Generated: never edit; a test fails if it is not the sheet's export | The exporter |
| `data/default.dta` | L4: rules, strategies, the designer's milestones | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/` | Scenarios: the first hour, and a ten-hour soak | The agent |
| `metrics/ledger.dta` | Objectives: pace windows, no runaway, no dead content, no dead ends, money growth | By pull request |

## Design

- **A day is a tick.** `Game::step` counts fixed steps and runs a day every
  `econ.day_seconds` of them; days are counted in steps, never summed seconds. The soak
  scenario runs its days directly with `{econ.advance_days 7200}`.
- **Markets fill.** A company sells into a market of its own: a good's price is
  `base_price × demand / (demand + saturation)`, selling adds to the saturation (revenue
  is the integral of the price curve), and each day `econ.recovery` of it clears. So a
  tenth farm earns less than the first, and moving up the chain pays.
- **Commands are data.** An unlock (`{{econ.total_earned} >= 10000}`) and a milestone's
  `when` are commands, evaluated each day about the company being judged; the economy
  itself never sees script.
- **Strategies** (`(strategies ...)`): greedy buys the best payback within its horizon and
  saves for it; the hoarder keeps a reserve and holds goods for a price (but not past ten
  days of output); the specialist builds toward one good's chain; random builds at random.
  None but random expands while losing money, and in debt each sells its least profitable
  building. `{econ.compete}` runs one company per strategy, each in its own market with the
  same days (the market's random draws depend only on the seed).
- **The spreadsheet stays first-class.** `economy.xlsx` is the source of the goods and
  buildings; the exporter (standard library only: an `.xlsx` is zipped XML) writes
  `economy.dta` with `(origin "economy.xlsx" (tool sheets-export))`, so `source()` names the
  sheet behind every number, and ends each line with its sheet and row. An agent tunes in
  `agent.dta` and reports the cells to change; a person changes the sheet and exports.

## What the objectives cannot see yet

The greedy bot wins every seed (`no_runaway` bounds its lead at under three times the
runner-up, and it is about 1.9). Whether that is a dominant strategy or simply the
competent player the pacing targets assume is a designer's call; a stronger hoarder or
specialist is the next bot to write.

## Measured

See `dev/decisions.md` D109.
