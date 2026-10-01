# BK fork — rules for agents

This is Noah's fork of QMK, which the Charybdis 4x6 firmware
(`NoahCLR/charybdis-4x6`) builds against. QMK's own conventions are in `docs/`.

## Every change follows the vault's rules

Every change in this repository, with or without a task from the work queue,
follows the work-queue vault's `AGENTS.md`
(`/Users/noah/dev/charybdis/charybdis-notes/AGENTS.md`), section **Branches,
landing and pushing**. Read it before your first change in a session. It covers
your own worktree and its `<type>/<slug>` branch (rename a branch the harness
made), `verify`, a draft pull request with `open-pr` linked to the thread (for
firmware and BK, with the pair to try), landing only on Noah's "land it" with
`land`, `release` as the only way `main` moves, and saying what each of Noah's
commands will do before asking for it. The tools enforce part of it; the rest
is yours to follow. This file still governs the code itself.

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
