# Reading a Studio Through Its Harnesses

Oct 10, 2026 · @Zach

## Why read a studio through its harnesses

A studio's harnesses are the most legible and most changeable record of how it actually works. Org charts describe intent, schedules describe hope, postmortems describe memory; the tools people spend their day inside describe practice. A boss fight gets tuned exactly as fast, by exactly the people, and with exactly the safety that its harness permits.

This document treats the harness tree from `harness-tree-vision.md` as a **diagnostic instrument**. It asks three questions of any studio: what environments does each role work inside, what qualities do those environments have, and how does material move between them? It draws on five bodies of work that rarely meet: empirical studies of game postmortems, developer-experience research, notation and end-user-programming theory, organization design, and the industry's own tooling talks.

The conclusion, stated up front: **most process problems in game development are visible first as harness problems** — a slow loop, a missing word, a layer only one person can extend, a tool users route around. Those are measurable, and Baker, as L0, is positioned to measure them. Section 7 turns the research into an audit method; section 9 says what Baker should change.

Companion to `harness-tree-vision.md`, `pushing-up.md`, `iteration-backgrounder.md`, and `baker-spec.md`. Layers L0–L4 are as defined in the vision doc.

## 1. What the empirical record says

The academic record on game development process is thin but consistent: games differ from other software mainly in how much the requirements churn, and the largest studies attribute most failures to people and organization rather than technology. Both findings bear directly on harnesses.

**Games churn more, so their soft layers must absorb churn.** [Murphy-Hill, Zimmermann, and Nagappan (ICSE 2014)](https://www.microsoft.com/en-us/research/publication/cowboys-ankle-sprains-and-keepers-of-quality-how-is-video-game-development-different-from-software-development) interviewed 14 developers and surveyed 364 who had worked both in and out of games. They found game teams rely on improvising programmers to cope with the constant collision between creative wishes and technical limits, and avoid automated tests because tests go stale as designers change their minds. Their recommendation was testing tools that can assert flexible behavior with little up-front investment. In harness terms: the churn lives at L3–L4, and any harness that makes those layers expensive to change, or that couples tests to them, will be routed around.

**Postmortems are the industry's own dataset.** [Washburn et al. (ICSE 2016)](https://swag.uwaterloo.ca/publications/what-went-right-and-what-went-wrong-an-analysis-of-155-postmortems-from-game-development.html) coded 155 Gamasutra postmortems and linked project characteristics to good and bad outcomes. [Politowski et al. (Information and Software Technology, 2021)](https://arxiv.org/abs/2009.02440v2) went further: 200 postmortems from 1997 to 2019, yielding 927 problems in 20 types.

Politowski's headline results are worth taking seriously because at first glance they cut against this document's thesis:

- Management and production problems appear in roughly equal proportion.
- Production problems stayed constant over 23 years, while technical and game-design problems declined.
- Team-related problems rose over the last decade.
- Most root causes are people, not technologies.

### Reconciling "it's people" with "read the harnesses"

The two are compatible once a harness is understood as a sociotechnical object. A harness encodes who may change what, how fast, and with whose permission. Several of the "people" problems in the postmortem literature are visible in harness structure before they appear in a retrospective:

| Postmortem complaint | How it shows up in the harness tree |
| --- | --- |
| Unclear or shifting vision | L2 vocabulary never stabilizes; game-level words keep getting renamed or deleted |
| Feature creep, late scope cuts | Discovery happens late because L4 loops are slow; problems surface only at integration |
| Crunch | The cost of a change grows toward ship; loop times degrade as content scales |
| Communication between disciplines | Requests between layers travel by ticket, not by a shared artifact like a stub or a shape |
| Key-person dependency | A layer that only one person can extend |

The harness lens does not explain everything. Market, funding, and leadership failures leave few traces in tools. What it offers is a set of **leading indicators** for the subset of problems that are expressed through the cost of change, which is where game projects most often bleed time.

One caution applies to everything in this section: postmortems are self-reported, written after the fact, and published selectively. They over-represent shipped games and narratable problems. Treat them as hypotheses about where to look, not as measurements.

## 2. Lens one: developer experience, per layer

Developer-experience research gives the harness tree a measurement vocabulary, but only once it is applied **per layer and per role** rather than to "engineering" as a whole. The frameworks were built for code delivery; in a studio, a designer tuning cadence at L4 and an engineer exposing a primitive at L1 are in different loops with different costs.

### DevEx: three kinds of friction

[Noda, Storey, Forsgren, and Greiler (ACM Queue, 2023)](https://queue.acm.org/detail.cfm?id=3595878) reduce developer experience to three dimensions: **feedback loops** (speed and quality of response to an action), **cognitive load** (mental processing a task requires), and **flow state** (sustained, energized focus). They argue these three together cover the full range of friction developers face, and recommend measuring them with both surveys and system data. A follow-up study [reported in 2024](https://visualstudiomagazine.com/articles/2024/01/23/devex-research.aspx) found developers with protected deep-work time felt about 50% more productive, and those who understood the code they worked in felt about 42% more productive.

Each dimension maps onto a harness property from the vision doc:

| DevEx dimension | Harness property | What to measure at each layer |
| --- | --- | --- |
| Feedback loops | Closes a loop to the running game | Edit-to-feel time: seconds from saving a change to seeing it on target hardware |
| Cognitive load | Speaks the domain's vocabulary; bounds what can be expressed | How many concepts from a harder layer a user must understand to do a routine task |
| Flow state | All four, together | How often a session is interrupted by a rebuild, restart, ticket, or wait on another role |

The last column is the important move. A designer whose loop is three seconds but who must file a ticket for every new word has a fast loop and broken flow. A gardener with a rich vocabulary but a two-minute reload has low cognitive load and broken flow. Flow is the composite, and it is the one that degrades first when a layer is wrong.

### SPACE: don't measure only activity

[Forsgren et al.'s SPACE framework (ACM Queue, 2021)](https://queue.acm.org/detail.cfm?id=3454124) names five dimensions — satisfaction and well-being, performance, activity, communication and collaboration, efficiency and flow — and explicitly rejects the idea that productivity is only about tools, only about individuals, or capturable by one metric. For harnesses, two of its points matter most. Satisfaction with tools is a leading indicator, so a harness people dislike is a problem before it is a slowdown. And activity counts — number of reloads, number of tweaks touched — are easy to collect and easy to misread; many reloads may mean a fast loop or a confusing one.

### DORA, translated to content

[DORA's four keys](https://dora.dev/guides/dora-metrics-four-keys) measure delivery throughput (change lead time, deployment frequency) and stability (change failure rate, failed-deployment recovery time), and DORA reports that top performers do well on all four at once. Each has a direct analogue for content moving through a harness:

| DORA key | Harness analogue | Baker hook that could measure it |
| --- | --- | --- |
| Change lead time | Time from an idea at L4 to its value in version control | Tweak edit timestamp → write-back commit |
| Deployment frequency | Reloads per working hour, per role | `Config::reload` count |
| Change failure rate | Share of reloads that punt, error, or crash | Reload callback outcomes; located errors |
| Recovery time | Time to revert a bad value or layer | `remove_layer` and provenance |

The DORA lesson that transfers best is the pairing: speed metrics without stability metrics reward breaking things faster. A harness that reloads in a second but punts half the time is not fast in any sense a designer would recognize.

## 3. Lens two: notations and end-user programming

The best tool for evaluating a *single* harness is thirty years old and was built for exactly this problem: the Cognitive Dimensions of Notations. DevEx tells you a layer hurts; Cognitive Dimensions tells you why, in terms a tools engineer can act on.

### Cognitive Dimensions: a profile per harness

Thomas Green introduced the framework and developed it with Marian Petre; their 1996 analysis of visual programming environments is the canonical application, and [Green and Blackwell's tutorial](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/CDtutorial.pdf) is the practical entry point. Green and Blackwell call the dimensions *discussion tools*: a lightweight vocabulary for comparing designs, not a scoring system. The framework applies to any information artifact, interactive or static — which includes a tuning panel, a node graph, a spreadsheet, or a `.dta` file.

Two features make it unusually fit for harness work. First, dimensions **trade off**: [the standard example](https://en.wikipedia.org/wiki/Cognitive_dimensions_of_notations) is that adding abstractions reduces viscosity but raises other costs, such as hidden dependencies. That is the push-up dilemma from `pushing-up.md` stated in HCI terms. Second, the framework names **design manoeuvres** — changes meant to improve one dimension — which is what a harness intervention is.

The dimensions most diagnostic for game harnesses, each with the question to ask:

| Dimension | Question for a harness | Typical game symptom | Baker feature that addresses it |
| --- | --- | --- | --- |
| Viscosity | How much work is one conceptual change? | Retuning one attack means editing twelve encounter files | Macros, `#merge`, script functions |
| Hidden dependencies | Can you see what depends on this value? | Changing a shared curve silently alters three bosses | Provenance; demand report call sites |
| Progressive evaluation | Can you run something half-finished? | A level won't load until every reference resolves | Stubs; unbound tweaks default rather than fail |
| Premature commitment | Must you decide before you know? | Schema field types fixed before the mechanic is found | Self-describing data; optional shapes |
| Abstraction gradient | What must you learn before doing anything? | A designer must understand the ability graph to change a cooldown | Generated panels over shapes |
| Closeness of mapping | Does the notation match the domain? | Tuning "aggression" as five unrelated floats | L2 vocabulary named in designer terms |
| Secondary notation | Can users add meaning the system ignores? | No place to write "0.6 is unfair on handheld" | `note` metadata, preserved by write-back |
| Error-proneness | Does the notation invite slips? | A typo in a symbol is a silent no-op | `baker::check`; located errors |
| Role-expressiveness | Can a reader tell what each part is for? | A wall of numbers with no grouping | `desc`, `group`, dotted prefixes |
| Provisionality | Can you sketch without committing? | No way to try a value without overwriting the shared one | Layers: write to `local`, promote later |

A **dimensions profile** — a row per harness, a column per dimension, filled in by the people who use it — is the single most useful artifact an audit can produce. It turns "the encounter tool is annoying" into "the encounter tool has high repetition viscosity and hidden dependencies through shared phase templates," which points at a specific manoeuvre.

### Attention investment: when people will build vocabulary

Alan Blackwell's [attention investment model](https://www.cl.cam.ac.uk/~afb21/publications/HCC02a.pdf) treats every act of abstraction as an economic decision. As a [later summary](https://arxiv.org/pdf/2311.00382) puts it, a user automates when the attention the automation is expected to save, net of what it costs to build, beats doing the work by hand — after discounting for the risk the automation doesn't work. Blackwell, Peyton Jones, and Burnett later used this model with Cognitive Dimensions to design user-defined functions for Excel, which is the closest published analogue to gardeners writing script functions in data.

The model explains three harness-tree phenomena precisely:

- **Crystallization timing.** The vision doc's "third time" rule is an attention-investment threshold. It moves earlier when the cost of defining a word drops (a macro in the same file) and later when it rises (a C++ rebuild and review).
- **The fled harness.** Users route to a spreadsheet because it lowers *perceived* cost and *perceived* risk — they already know it, and mistakes are visible. Improving an official harness without lowering those perceptions will not bring them back.
- **Stubs.** A stub reduces the risk term: the gardener invests a few minutes, and the designer's investment in tuning against it is protected because the call site won't change when the real implementation lands.

### Learning barriers and the gentle slope

Ko, Myers, and Aung's six learning barriers for end-user programmers — design, selection, coordination, use, understanding, and information — are a checklist for whether a would-be gardener can actually extend a layer. The Ink & Switch "gentle slope" cited in the vision doc is the same idea from the other side: each step from user to creator should cost a little more than the last, never a cliff. In a harness tree, the cliffs are almost always at layer boundaries where the notation changes — from panel to data file, or from data file to C++. Baker's single notation from L1 to L4 is, in these terms, an attempt to remove three of the four cliffs.

## 4. Lens three: the harness tree is an org chart

Every layer boundary in the harness tree is also a boundary between people, and the research on organization design says the two will converge whether or not anyone plans it. So the harness tree should be read, and designed, as org structure.

### Conway's law has evidence behind it

[MacCormack, Baldwin, and Rusnak (Research Policy, 2012)](https://dash.harvard.edu/handle/1/34403525) tested the "mirroring hypothesis" — that products reflect the organizations that build them — on matched pairs of software products serving the same function. In every pair, the product from the loosely coupled organization was significantly more modular than the product from the tightly coupled one. Later work argues the effect is real but contingent on factors such as product complexity, so it is a strong tendency rather than a law.

For a studio, the implication runs both ways:

- **Reading:** the harness tree you have tells you who actually talks to whom. A layer boundary with no corresponding handoff between people tends to erode; a handoff with no layer boundary tends to become a ticket queue.
- **Designing:** adding a layer is an organizational act. Creating an L3 task harness without a gardener role to own it produces an orphan; hiring technical designers without giving them a layer they can extend produces frustrated engineers-in-waiting.

### Team Topologies maps onto the layers

Skelton and Pais's *Team Topologies* is the most usable current model for this, and it is built on the same quantity DevEx uses: cognitive load. It names four team types and three interaction modes. As [Martin Fowler summarizes](https://martinfowler.com/bliki/TeamTopologies.html), a platform's main benefit is reducing the cognitive load of the teams that use it, it should be mostly self-service, and collaboration with its users is a temporary mode until the platform matures into X-as-a-Service.

| Team Topologies type | Studio equivalent | Harness layer it owns | Healthy interaction mode |
| --- | --- | --- | --- |
| Stream-aligned | Mission, encounter, hero, or level teams | Works at L4; owns L3 instances | Consumes L2 as a service |
| Platform | Engine/tools team; game-framework team | L1 and L2 | X-as-a-Service once stable; collaboration while a vocabulary is being found |
| Enabling | Technical designers and technical artists as coaches | Builds L3 for and with a stream team, then hands it over | Facilitating |
| Complicated subsystem | Rendering, physics, networking, audio | Specialist corners of L1 | X-as-a-Service |

The gardener deserves a note. Nardi's gardeners sit awkwardly in Team Topologies: they are partly enabling (they build harnesses and teach), partly stream-aligned (they often live with one content team). That ambiguity is real and worth naming in any studio audit, because unclear gardener ownership is the most common way L3 harnesses rot.

Two further Team Topologies ideas transfer directly:

- **Three kinds of cognitive load.** Intrinsic load belongs to the task, extraneous load to the environment ("how do I reload this again?"), and germane load to the learning that makes someone better at the task. A good harness removes extraneous load and protects germane load. A designer should spend their attention on how phase three *feels*, not on how to get the build to pick up a change.
- **Thinnest viable platform.** A platform should be no thicker than needed, sometimes just a wiki page. For harnesses this is Shumaker's "don't push up speculatively" applied to whole layers: an L2 vocabulary should be grown from demand, not designed in advance.

### Golden paths and the voluntary-use test

Spotify coined the [golden path](https://engineering.atspotify.com/2020/8/how-we-use-golden-paths-to-solve-fragmentation-in-our-software-ecosystem) as the opinionated, supported way to build something; teams may deviate, but without the same support. The golden-path tutorials became the company's most-read technical documentation. [Red Hat's guidance](https://www.redhat.com/it/blog/designing-golden-paths) adds the part most relevant here: platform teams should watch where golden paths are *not* used, because that is where the next path is needed.

In a studio, every layer should have a golden path for its most common task — "add a new ability," "make a phase tunable" — and departures from it are data. This pairs with a blunt test borrowed from platform engineering: if the mandate to use a harness disappeared tomorrow, would people keep using it? A harness that fails this test is already a fled harness; it just hasn't been abandoned yet.

## 5. Lens four: harnesses evolve, and the right practice depends on how far

The harness tree has a vertical axis — hardness, L0 to L4 — but the vision doc's "crystallize" and "prune" phases imply a second, horizontal axis: how mature each piece of vocabulary is. Simon Wardley's evolution model supplies that axis, and plotting a studio's vocabulary on both at once is the clearest single picture of its harness health.

### Wardley's evolution axis

Wardley maps place components on an evolution axis running through [four stages](https://wardleymaps.com/learn): genesis, custom-built, product, and commodity. The method's central claim is that the right approach depends on the stage — [build at genesis, contract custom work, buy products, rent commodities](https://agilebrandguide.com/wiki/models/wardley-mapping/) — and that inertia from past success is what stops components moving.

A harness vocabulary item moves along the same axis:

| Stage | Harness example | Right home | Right practice |
| --- | --- | --- | --- |
| Genesis | "Retreat to cover," first asked for this week | L3–L4, as a stub or script | Cheap, disposable, tuned by playing |
| Custom-built | Cover scoring, now copied into three encounters | L3, as a shared script function | Consolidate; give it a name and a shape |
| Product | `encounter.find_cover`, used across the game | L2, native, weights in data | Stabilize signature; document; measure |
| Commodity | Config reload, layering, provenance, serialization | L0, ideally not built in-house | Standardize or adopt; never reinvent per game |

Two things follow.

**Crystallization is movement rightward, and pushing up is usually movement downward too.** The `pushing-up.md` worked example is a component travelling from genesis at L3 to product at L2. Wardley's framing adds a warning: moving a component right *before* it has stabilized freezes the wrong design. That is the formal version of "don't push up speculatively."

**Commodity work should be commodity.** Reload, layers, provenance, write-back, and a command channel are the same in every game; they are commodity infrastructure. Every studio that writes its own is spending genesis-grade effort on a commodity component. That is the strategic argument for Baker as a library rather than a pattern: it moves L0 to the right end of the axis for any studio that adopts it.

### Pioneers, settlers, town planners — and gardeners

Wardley pairs the stages with three kinds of people: pioneers who invent, settlers who turn prototypes into products, and town planners who industrialize. His sharpest claim is that two-part structures — R&D and operations, with nothing in between — fail, because [the transitional middle layer](https://wardleymaps.com/learn) is what moves work forward. Pioneers lose interest before their prototypes are products; town planners reject prototypes as too unreliable.

This is the gardener argument again, from strategy rather than HCI. In a studio:

- **Pioneers** are designers and prototypers working at L4 and in stubs.
- **Settlers** are gardeners — technical designers and technical artists — who turn repeated soft constructions into named vocabulary at L3 and L2.
- **Town planners** are engine and tools engineers hardening and industrializing L1 and L0.

A studio with designers and engine programmers but no gardeners is exactly Wardley's failed bimodal organization. Its symptoms are recognizable: prototypes that never become features, and engine work that never reaches designers.

### Inertia and the Halo lineage

Wardley's concept of inertia — past success resisting change — names the pattern in `iteration-backgrounder.md` §2. The engine that gave Halo 2 a three-second loop in 2004 became, two decades later, tooling cited as a reason to move to Unreal. A harness that was once at the product stage can stay in place while the rest of the industry turns it into a commodity; the studio is then running custom-built infrastructure for a commodity need. Periodic mapping is how that becomes visible before it becomes a migration.

## 6. What studios actually do

The industry has begun treating internal tools as products with users, but measurement of those tools still lags far behind measurement of the game itself. Studios profile frame times to the microsecond and rarely know how long it takes a designer to change a number. The examples below are the leading edge; `iteration-backgrounder.md` covers the loop-time war stories (Bungie, Bitsquid, Destiny, Unity) and is not repeated here.

### Tools as products, with UX and research

Internal-tool UX is now a recognized specialty. At GDC 2022, a Ubisoft internal-tool UX designer and user researcher [presented the revamp](https://gdcvault.com/play/1027943/Tools-Summit-Ubisoft-Connect-Building) of a decade-old release tool that had poor usability and invited human error, using a user-centered framework built on qualitative research and usability evaluation. GDC 2024 ran a [tools-design roundtable](https://schedule.gdconf.com/session/tools-design-roundtable-day-3-user-research/903347) devoted specifically to user-research methods for in-house tools. Live-ops studios now hire UX designers whose whole remit is internal operational tools.

The practical advice from tools roundtables is consistent and modest. [Notes from GDC 2021's tools roundtables](https://github.com/Kadino/gdconf_notes/blob/main/gdc2021.md) record two recurring recommendations: record metrics to find where time goes, and add telemetry for basic events in your tools. Few studios report having done it systematically.

### The economics of seconds

Small savings compound, and the industry's tool talks keep rediscovering it. A [summary of several GDC tool talks](https://brainhub.eu/library/custom-tools-in-game-development) describes a Playdead developer automating a fiddly spike-rotation task he did about 50 times a day, reclaiming roughly three hours a month; Croteam cutting bug reporting to about ten seconds, which raised both iteration speed and game quality; and an Epic designer at GDC 2010 arguing that even seemingly trivial time savings matter at studio scale.

The harness-tree reading: these are all **extraneous-load removals at L3–L4**, built by someone close to the work. They are exactly the disposable harnesses Dear ImGui made cheap, and they are almost never counted.

### Testing turned into a harness

Rare's *Sea of Thieves* is the clearest case of a studio overturning the Murphy-Hill finding that game developers avoid automated tests. Rare built automated gameplay testing in [from the start](https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features), with a framework designed so team members could create tests quickly and easily. Rare engineers also [presented at Unreal Fest Europe 2019](https://www.unrealengine.com/events/unreal-fest-europe-2019/automated-testing-at-scale-in-sea-of-thieves) on making thorough tests feel like a tool rather than a chore, in service of shipping the game as a constantly changing service. The approach was later [carried to *Minecraft*](https://gdcvault.com/play/1027345/Lessons-Learned-in-Adapting-the) with the explicit goal of a culture where developers write fast, reliable tests as part of daily work.

The lesson is not "test more." It is that Rare lowered the attention-investment cost of a test until it was worth paying, by building a *harness for writing tests*. The churn that makes tests go stale was absorbed by the framework, not by the people. That is the harness tree's answer to the cowboy-coder problem in §1.

### What this adds up to

| Practice | Who does it, publicly | Harness-tree reading |
| --- | --- | --- |
| Internal-tool UX and user research | Ubisoft; live-ops studios; GDC tools-design roundtables | Cognitive Dimensions and DevEx, done by practitioners without the vocabulary |
| Tool telemetry | Recommended at GDC tools roundtables; rarely reported | The missing instrument — DORA-style metrics for content |
| Micro-tools for repeated tasks | Playdead, Croteam, countless unreported | L3–L4 crystallization by attention investment |
| Testing as a harness | Rare, Mojang | Lowering the cost of a safety net until it is used |
| Sub-second reload | Bitsquid; Bungie in 2004 | The cross-cutting loop, done right at L0 |

The gap in the record is consistent: studios describe tools and anecdotes, rarely measurements across their whole tree. That gap is what the method in §7 is for.

## 7. A method: auditing a studio's harness tree

A harness audit takes about two weeks for a mid-sized team, combines stopwatch measurements with structured interviews, and produces four artifacts: a harness register, a loop-time table, a dimensions profile per harness, and an evolution map. It should be repeated once per milestone, and its results should never be used to evaluate individuals — SPACE's warning about activity metrics applies with full force.

### The steps

1. **Inventory the harnesses.** For each role, list every environment they work inside for more than an hour a week: editors, panels, spreadsheets, data files, scripts, consoles, and private tools. Include the unofficial ones; they are often the most informative. Record each in a register (template below).
2. **Time the loops.** For each role's five most frequent tasks, measure edit-to-feel time on target hardware with a stopwatch, by watching real work, not demos. Record the median and the worst case observed. Note every wait and handoff along the way, separating value-adding time from waiting, as a value-stream map would.
3. **Profile each harness on Cognitive Dimensions.** Interview two or three users per harness using the dimension questions from §3. Blackwell and Green published a user questionnaire for exactly this purpose. Record the answers as short phrases, not scores.
4. **Map who can extend what.** For each layer, answer three questions: who can add a new word without a rebuild, how many people can do it (fewer than two is a finding), and what interaction mode connects them to the layer's users. Mark where gardener ownership is unclear.
5. **Collect demand signals.** Unresolved words and stubs (Baker's demand report produces these automatically), cross-layer tickets, copy-pasted constructions, and recurring console command sequences. These are the specification for the next layer, per the vision doc's Phase 2.
6. **Collect flight signals.** Spreadsheets exported into the game, private scripts that bypass an official tool, and the answer to the voluntary-use question: if this harness were optional, would you use it? Flight is the strongest single indicator that a harness has failed property 4.
7. **Place vocabulary on the evolution map.** For the twenty or so most-used words at L2 and L3, mark their layer and their stage from genesis to commodity. Flag commodity capabilities built in-house and genesis-stage ideas already hardened into code.
8. **Choose three interventions and re-measure.** Use §8 to pick them. Re-run steps 2, 5, and 6 at the next milestone; the deltas are the result.

### The register

One row per harness. Columns chosen so that every one feeds a later step:

| Harness | Layer | Built by | Used by | Notation | Edit-to-feel (median / worst) | Extensible without rebuild by | Bus factor | Voluntary use? | Stage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Example: boss workbench | L3 | Technical designer | Encounter designers | Data + panel | 3 s / 40 s | Technical designers | 2 | Yes | Custom-built |
| Example: ability graph | L2 | Gameplay engineers | Designers, technical designers | Visual script | 25 s / 4 min | Gameplay engineers only | 1 | Mandated | Product |
| Example: economy sheet | L3 (unofficial) | A systems designer | Systems designers | Spreadsheet → export | 10 min / 1 h | Its author | 1 | Yes — preferred | Genesis |

The example rows show the kinds of finding the register surfaces at a glance: a bus factor of one on a mandated L2 harness, and an unofficial L3 harness that users prefer to the official one.

### Reading the results

A few patterns recur and map directly to remedies in §8:

- **A fast loop with high flight** means the vocabulary is wrong, not the speed. Look at closeness of mapping and abstraction gradient.
- **A slow loop with low flight** usually means there is no alternative, not that the harness is good. Treat it as a latent fled harness.
- **Demand piling up at one boundary** means that boundary's harder side has no settler. Check step 4 for a missing gardener.
- **Many in-house commodity components** at L0–L1 means engineering attention is being spent where the industry has already converged. That is an adoption opportunity.

## 8. Interventions, ranked by leverage

The highest-leverage interventions are the ones every layer inherits — the loop, the instruments, and the people at each boundary — and the lowest are new vocabulary built before those exist. Leverage here means how many harnesses and roles an intervention improves at once.

| Rank | Intervention | What it fixes | Research basis | Failure mode to watch |
| --- | --- | --- | --- | --- |
| 1 | **Fix the loop at L0**: sub-second reload on target hardware, layers, provenance | Feedback loops for every role; DORA-style lead time | DevEx; Frykholm; Butcher | Building rich vocabulary first, then discovering it can't reload |
| 2 | **Instrument the tree**: loop times, reload outcomes, demand and flight signals, collected automatically | Makes every other intervention measurable | DORA; SPACE; GDC tools roundtables | Metrics used to rank individuals; counting activity instead of friction |
| 3 | **Name a gardener at every soft boundary**, and at least two per layer | Missing settlers; single-gardener risk; unclear L3 ownership | Nardi; Wardley; Team Topologies enabling teams | Gardeners turned into a ticket queue for engineers' overflow |
| 4 | **Stub-first protocol for cross-layer requests** | Requests by ticket; flow broken while waiting on a harder layer | `pushing-up.md`; attention investment; collaboration → X-as-a-Service | Stubs that never get replaced and quietly become the product |
| 5 | **Write-back and personal layers** | Discoveries lost in running sessions; no safe place to sketch | Provisionality; secondary notation | Local layers that diverge and never get promoted |
| 6 | **Adopt, don't fight, the fled harness** | Spreadsheets and private tools running in parallel with the official path | Hutch Games; golden-path non-use as signal | Two sources of truth for the same values |
| 7 | **Generated panels from shapes for the most-tuned domains** | High abstraction gradient for designers | Cognitive Dimensions; Halo's generated UI | Premature commitment: schemas written before the mechanic is found |
| 8 | **A harness for writing tests over L2 vocabulary** | Late regressions; fear of changing shared vocabulary | Rare; Murphy-Hill | Tests written against churny L4 instances, which then rot |
| 9 | **Prune and commoditize** at each milestone | Accreted harnesses; in-house commodity infrastructure | Wardley inertia; vision doc Phase 5 | Removing something telemetry would have shown is still quietly in use |

Three principles govern the order.

**Loop before vocabulary.** Every war story in the backgrounder that ends badly has rich vocabulary and a slow loop. None of the success stories has the reverse. A team that can see a change in a second will build its own vocabulary; a team with good vocabulary and a two-minute reload will route around it.

**People before tools at the boundaries.** The organization-design research and the end-user programming research converge on the same point from different directions: a layer is only extensible if someone in the right role can extend it. Tooling for a boundary nobody owns is wasted.

**Measure friction, not activity.** The only metrics worth collecting are the ones that describe what makes work hard — wait time, interruptions, failed reloads, unresolved words, flight. Counts of commits, reloads, or tweaks touched invite exactly the misuse SPACE warns against.

## 9. What this changes in Baker

Baker sits under every harness in the tree, so it is the cheapest place in a studio to put the instruments the audit in §7 needs. Five changes, all observational, all small, all compiled out under `BAKER_SHIPPING`. The library still owns no I/O, sockets, or threads; it records and hands the host data through callbacks.

1. **Loop telemetry.** `Config` records, per reload, the time from file-change notification to reload complete, the per-file parse and merge time, and each callback's outcome (`Migrated`, `Punt`, or error). A host may call `cfg.mark_visible()` when the next frame presents, closing the edit-to-feel loop. `cfg.loop_stats()` returns rolling medians and worst cases per file and per layer. This is DORA's lead time and change-failure rate for content, measured where it actually happens.
2. **An edit journal.** Every live change to a tweak or config value in a session — from a panel, the console, or `eval` — is appended to `cfg.journal()` with old value, new value, provenance, and whether it was later written back. At shutdown or on request the host can list unsaved discoveries. This turns "tuned values lost in the running session" from an anecdote into a count, and gives write-back (§4 of the vision doc) its success metric.
3. **Harness metadata on packs.** The pack declaration gains optional fields: `(pack boss (layer task) (requires game engine) (owners "td-encounter" "td-combat") (for designers) (stage custom))`. `write_reference` emits them, so the generated reference doubles as the harness register from §7. `check` reports packs with fewer than two owners as an informational finding — the single-gardener failure mode, surfaced from data.
4. **A repetition report.** `baker::check` hashes command and array subtrees structurally and reports any non-trivial subtree appearing three or more times across the files it was given, with every location. These are crystallization candidates — the vision doc's "third time" rule, computed rather than noticed. It complements the demand report: demand shows words the soft layers are missing, repetition shows words they have already built by hand.
5. **Origin provenance for external harnesses.** A merged or included file may declare `(origin "economy.xlsx" (tool sheets-export))`. Provenance carries the origin through to every value, so `source()` can answer "this number came from the economy spreadsheet via an export," and the reference can list which external tools feed the config. Fled harnesses become visible and traceable instead of silently competing with the official path.

The demand report, repetition report, and pack metadata should share one stable, documented, machine-readable output format, so a studio can diff them milestone to milestone. A harness audit then becomes partly a build artifact: steps 1, 5, and 6 of §7 are generated, and steps 2 and 4 are pre-filled.

**Size.** Items 3–5 live in `baker::check` and the parser's pack handling and should fit in a few hundred lines. Items 1–2 add counters and a ring buffer to `Config`. None touches the node layout or the evaluation path. If any pushes the core past its 4,000-line budget, item 4 is the one to move into a separate tool.

**Considered and not adopted.** Built-in survey prompts, user identity, and any network reporting. Measuring perceived experience matters — DevEx and SPACE both insist on it — but it belongs to the studio's tooling, not to a configuration library. Baker should make the system data trivially available and stop there.

## Sources

Empirical studies of game development

- Murphy-Hill, Zimmermann, Nagappan, ["Cowboys, Ankle Sprains, and Keepers of Quality: How Is Video Game Development Different from Software Development?"](https://www.microsoft.com/en-us/research/publication/cowboys-ankle-sprains-and-keepers-of-quality-how-is-video-game-development-different-from-software-development), ICSE 2014.
- Washburn, Sathiyanarayanan, Nagappan, Zimmermann, Bird, ["What Went Right and What Went Wrong: An Analysis of 155 Postmortems from Game Development"](https://swag.uwaterloo.ca/publications/what-went-right-and-what-went-wrong-an-analysis-of-155-postmortems-from-game-development.html), ICSE 2016.
- Politowski, Petrillo, Ullmann, Guéhéneuc, ["Game Industry Problems: An Extensive Analysis of the Gray Literature"](https://arxiv.org/abs/2009.02440v2), Information and Software Technology, 2021.

Developer experience

- Noda, Storey, Forsgren, Greiler, ["DevEx: What Actually Drives Productivity"](https://queue.acm.org/detail.cfm?id=3595878), ACM Queue, 2023; follow-up study [via Visual Studio Magazine](https://visualstudiomagazine.com/articles/2024/01/23/devex-research.aspx), 2024.
- Forsgren, Storey, Maddila, Zimmermann, Houck, Butler, ["The SPACE of Developer Productivity"](https://queue.acm.org/detail.cfm?id=3454124), ACM Queue, 2021.
- DORA, ["DORA's software delivery metrics: the four keys"](https://dora.dev/guides/dora-metrics-four-keys).

Notation and end-user programming

- Green and Blackwell, [*Cognitive Dimensions of Information Artefacts: a tutorial*](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/CDtutorial.pdf); Green and Petre, "Usability analysis of visual programming environments: a 'cognitive dimensions' framework," 1996; [overview](https://en.wikipedia.org/wiki/Cognitive_dimensions_of_notations).
- Blackwell, ["First Steps in Programming: A Rationale for Attention Investment Models"](https://www.cl.cam.ac.uk/~afb21/publications/HCC02a.pdf), HCC 2002; [summary in later end-user programming survey](https://arxiv.org/pdf/2311.00382).
- Ko, Myers, Aung, "Six Learning Barriers in End-User Programming Systems," VL/HCC 2004.
- Nardi, *A Small Matter of Programming*, MIT Press, 1993 (via `harness-tree-vision.md`).

Organization design and strategy

- MacCormack, Baldwin, Rusnak, ["Exploring the duality between product and organizational architectures: A test of the 'mirroring' hypothesis"](https://dash.harvard.edu/handle/1/34403525), Research Policy, 2012.
- Skelton and Pais, *Team Topologies*, IT Revolution, 2019; [Martin Fowler's summary](https://martinfowler.com/bliki/TeamTopologies.html).
- Spotify Engineering, ["How We Use Golden Paths to Solve Fragmentation in Our Software Ecosystem"](https://engineering.atspotify.com/2020/8/how-we-use-golden-paths-to-solve-fragmentation-in-our-software-ecosystem), 2020; Red Hat, ["Designing Golden Paths"](https://www.redhat.com/it/blog/designing-golden-paths).
- Wardley mapping: [Wardley Maps learning resources](https://wardleymaps.com/learn); [stage-by-stage approach summary](https://agilebrandguide.com/wiki/models/wardley-mapping/).

Game-industry practice

- Masella, ["Automated Testing of Gameplay Features in 'Sea of Thieves'"](https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features), GDC 2019; Baker, ["Automated Testing at Scale in Sea of Thieves"](https://www.unrealengine.com/events/unreal-fest-europe-2019/automated-testing-at-scale-in-sea-of-thieves), Unreal Fest Europe 2019; Golding, ["Lessons Learned in Adapting the 'Sea of Thieves' Automated Testing Methodology to 'Minecraft'"](https://gdcvault.com/play/1027345/Lessons-Learned-in-Adapting-the), GDC 2021.
- Ko and Panic-Cidic, ["Ubisoft Connect: Building User-Centered Frameworks for Internal Tools"](https://gdcvault.com/play/1027943/Tools-Summit-Ubisoft-Connect-Building), GDC 2022 Tools Summit.
- GDC 2024 [Tools Design Roundtable: User Research](https://schedule.gdconf.com/session/tools-design-roundtable-day-3-user-research/903347); [community notes from GDC 2021 tools roundtables](https://github.com/Kadino/gdconf_notes/blob/main/gdc2021.md).
- [Summary of GDC tool talks by Playdead, Croteam, and Epic](https://brainhub.eu/library/custom-tools-in-game-development) (secondary source).

Companion documents: `harness-tree-vision.md`, `pushing-up.md`, `iteration-backgrounder.md`, `halo-tags-vs-dataarray.md`, `seexpr-vs-baker.md`, `baker-spec.md`.
