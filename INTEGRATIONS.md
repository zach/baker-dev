# Integrations

Baker is MIT-licensed, and it stays that way: free to use, change and ship in anything,
open or closed, with no other party's terms attached. This policy is how Baker is tried
with platforms whose terms are more restrictive without any of those terms reaching Baker,
in fact or in appearance:

- copyleft code (GPL, AGPL, and LGPL beyond its linking allowance);
- source-available code (Business Source License, SSPL and the like), for example a
  database such as SpacetimeDB;
- commercial and proprietary SDKs and engines;
- platforms under NDA.

Baker's features may be motivated by such integrations. Baker's code, documentation and
design are never derived from them.

This is a working policy, not legal advice. Before an integration ships, or when a target's
terms are unclear, the owner has its license reviewed.

## The rules

1. **Dependencies point one way.** An integration depends on Baker; Baker never depends on
   an integration or on its target. Nothing in this repository includes, vendors, links to,
   builds against, or tests against a restrictively licensed target, not even optionally or
   behind a CMake switch.

2. **Each integration lives in its own repository**, under the license its target requires
   or allows.
   - An integration with a GPL engine may be GPL. Baker's MIT code can be combined into a
     GPL work; the reverse cannot happen, which is why the arrow points the way it does.
   - An integration that touches NDA or commercial material is a private repository.
   - `stacks/` sits inside this repository only because the stacks are MIT and use nothing
     restrictive. An integration with restrictive terms never lives here.

3. **Needs cross into Baker as requirements, never as code.** When an integration needs
   something Baker lacks, the need is written down in the integration's repository as a
   requirement in Baker's own terms (the template is in `tools/integration-template/`). A
   requirement names:
   - what a host needs (for example, that the host supplies a file's bytes);
   - why, as a general use case;
   - the constraints (allocation, latency, threading).

   It never copies the target's code, identifiers, structure or documentation. Baker's change
   is then designed and written here from the requirement alone, as a Baker feature, with
   Baker's own tests against a mock host.

4. **Facts may cross; expression may not.** Baker may rely on how a system behaves: what it
   calls, in which order, which formats it accepts, how fast it must be. It may not take how
   that system is written. A Baker interface is named and shaped for Baker's users, not to
   mirror a target's classes or functions. "A pluggable reader" is a Baker feature. "A copy
   of X's file system API" is not.

5. **Confidential information stays where it was given.** What a commercial SDK or an NDA
   discloses goes into the private integration repository and nowhere else: not into this
   repository's code, docs, decision records, commit messages, issues or pull requests, and
   not into an agent session working on Baker itself.
   - A requirement written for Baker from such an integration states the need generically,
     with nothing that identifies or describes the confidential material.
   - If a need cannot be stated without disclosing something, it waits until it can.

6. **Baker's record describes its own reasons.** A decision record or changelog entry gives
   the general motivation, such as "hosts that stream content from packages".
   - It may name a public product as an example user, the way the docs name engines and
     games.
   - It never describes a target's internals, and it never presents a Baker feature as
     derived from or equivalent to another product's.

7. **Provenance is kept.**
   - Every change to Baker comes from someone entitled to contribute it. Contributions from
     anyone other than the owner carry a Developer Certificate of Origin sign-off
     (`git commit -s`, <https://developercertificate.org>).
   - Code read in a restrictively licensed project is not reproduced from memory here. Work
     on an integration and work on Baker happen in separate sessions, whether a person or an
     agent does them.

8. **Names are used descriptively.** An integration may describe itself as "Baker for X"
   or "a Baker integration with X". It does not use X's logos or suggest that X endorses
   Baker, or Baker X, unless the owner has that permission in writing.

## In practice

| Where | What it holds | License |
|---|---|---|
| this repository (baker-dev), and zach/baker as published | Baker; generic features any host can use; mock hosts in Baker's tests | MIT |
| `baker-<target>` (one per integration) | glue between Baker and the target; requirement notes; tests against the target | whatever the target requires; private when it touches confidential material |

The order of work for a feature an integration motivates:

1. The integration hits a limit, and works around it locally if it can.
2. A requirement note is written in the integration's repository, in Baker's terms.
3. The note's text, and only its text, is brought here as an issue or a design note.
4. The feature is designed and built here as Baker's own, with a decision record giving the
   general motivation, and tests against a mock host.
5. A Baker release ships it. The integration takes the new version and drops its
   workaround.

When in doubt whether something may cross, it does not; ask the owner.
