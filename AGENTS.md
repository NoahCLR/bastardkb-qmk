# BK fork — rules for agents

This is Noah's fork of QMK, which the Charybdis 4x6 firmware
(`NoahCLR/charybdis-4x6`) builds against. QMK's own conventions are in `docs/`.

## How a change is made

Only Noah works on this repository. His private notes vault, `charybdis-notes`,
sits beside this repository's main checkout (from any worktree:
`"$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")/../charybdis-notes"`). It holds the work queue and the tools every change goes through.
If it is there, read its `AGENTS.md` before your first change in a session:
**Every agent, wherever it starts** (work starts from a note or right here,
and Noah's commands have skills) and **Branches, landing and pushing** (your
own worktree on a `<type>/<slug>` branch from `noah-userspace-contracts-dev`, `verify`, a draft pull
request with `open-pr` linked to the thread (with the firmware pair to try), landing only on Noah's
"land it" with `land`, `release` as the only way the released line moves, and saying
what each of Noah's commands will do before asking for it). The tools enforce
part of it; the rest is yours to follow. This file still governs the code
itself.

Without the vault: branch from `noah-userspace-contracts-dev` as `<type>/<slug>`, run the checks this
file lists, and open a pull request into `noah-userspace-contracts-dev` on `NoahCLR/bastardkb-qmk`. Never
push `noah-userspace-contracts-dev` or the released line directly, and nothing goes upstream. Noah lands
and releases.

## This repository

- **Keep patches small and hook-shaped**, so syncing with upstream QMK stays
  cheap. Userspace policy belongs in the firmware repository, not here.
- **Branches.** `noah-userspace-contracts-dev` is the trunk: branch from it and
  land onto it. `noah-userspace-contracts` is the released line (the GitHub
  default branch); only a firmware release moves it, to the commit firmware
  pins. `main` is an old upstream mirror: never push it.
- **Pull requests merge as merge commits**, never squashed, so a commit the
  firmware pins stays valid and an upstream sync keeps QMK's history (`land`
  does this).
- **Audit the fork before verification.** Run `python3 util/check_noah_fork.py`
  and `python3 -m unittest discover -s util -p test_noah_fork.py`. The
  inventory must explain every upstream difference; runtime and board patch
  fingerprints require explicit review when changed. Never refresh them just
  to silence a failure. For upstream syncs, also run the named behavioral tests
  with `--firmware PATH --run-tests` (see `docs/noah-fork-contracts.md`).
- **Verify through the firmware.** The vault's `verify` runs the firmware's
  host suite and builds the flashable pair against your branch; BK has no CI of
  its own (GitHub Actions stay off, so QMK's upstream workflows never run).
- **Land BK before the firmware that pins it.** The firmware picks a BK change
  up in its own pull request, re-pinning `qmk-pin.json` (`sh tools/pin-qmk.sh`
  in the firmware).
- **Upstream syncs** follow [the README](README.md#syncing-with-upstream-qmk):
  branch `chore/sync-upstream-qmk-<version>`, merge (never rebase) upstream in,
  verify, land as a merge commit, then re-pin the firmware.
- **Nothing goes upstream.** This is a GitHub fork of Bastard Keyboards'
  repository: push only to `NoahCLR/bastardkb-qmk` and never open a pull
  request anywhere else. The clone's `gh` default and push hook enforce this.
