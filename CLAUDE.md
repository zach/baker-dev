# Working on Moira

## Rules

- **Breaking changes are allowed and encouraged.** Do not keep backward-compatibility
  layers, deprecated aliases, or unused legacy code paths. When an API changes, change
  every caller in the repository and delete the old form. There are no downstream users
  to protect. (Script-level `Status::Deprecated` is a feature for script authors, not a
  compatibility layer.)
- Record each deviation from `docs/spec.md` in `docs/decisions.md` (spec §11).
- Before every commit, run `tools/local_matrix.sh`; every configuration must be green.
- Hold designs and hot-path changes to `.claude/skills/acton-review/`.
