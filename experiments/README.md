# Syntax experiments

Sketches of other text forms for Baker, each on its own branch
(`claude/fervent-cray-l1kyf0-syntax-<slug>`), each with a design note at
`dev/syntax-<slug>-design.md`. `dev/syntax-ideas.md` on the session branch compares them.
Nothing here ships (`tools/release/publish.py` publishes `src/`, `include/`, `docs/`,
`examples/` and the files the release process names), and nothing here counts toward the
core's line budget (`tools/count_lines.py` reads `src/` and `include/`). Build with
`-DBAKER_BUILD_EXPERIMENTS=ON`.

An experiment is a **front end**: a reader of its text form into Baker's existing tree, so
every tool keeps working on what it reads, and a printer of the tree into that form, so
the corpus can be converted and a round trip can be tested. It uses Baker through the
public headers and may use the STL (it is a tool; the container rule is for `src/`). If an
idea needs a change the core cannot be shown without (a node kind, a lexer rule), the
branch makes the smallest one behind a switch and its note states the line cost.

`common/` is what every experiment shares: the corpus (`corpus_files`), tree comparison
(`first_difference`), the round trip over the corpus (`round_trip_corpus`), and the parse
benchmark against Baker's reader on the same content (`compare_parsers`).

`syntax-template/` is the identity experiment: its reader is `baker::read_string` and its
printer is `baker::to_dta`, so it passes the round trip and benchmarks at 1.0. Copy it to
start one:

```sh
cp -r experiments/syntax-template experiments/syntax-<slug>
# rename the targets in its CMakeLists.txt, then replace syntax.cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBAKER_BUILD_EXPERIMENTS=ON
cmake --build build --target baker_syntax_<slug> baker_syntax_<slug>_tests
ctest --test-dir build -R syntax_<slug>
build/experiments/syntax-<slug>/baker_syntax_<slug> check      # the corpus round trip
build/experiments/syntax-<slug>/baker_syntax_<slug> bench      # against baker::read_string
build/experiments/syntax-<slug>/baker_syntax_<slug> to-new stacks/duel/data/default.dta
```

Every experiment provides, in its directory:

- `syntax.hpp` / `syntax.cpp`: `read(text, name)` and `print(root)`.
- `main.cpp`: the CLI (`to-new`, `to-dta`, `check`, `bench`).
- `tests.cpp`: the corpus round trip, a case for every new form, and error cases.
- `samples/`: real files from `stacks/` and `examples/` in the new form, beside the
  originals.
- A `README.md` of one paragraph pointing at the design note.
