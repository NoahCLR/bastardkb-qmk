# QMK for Noah's Charybdis 4x6

This is my fork of [QMK](https://github.com/qmk/qmk_firmware) that
[my Charybdis 4x6 firmware](https://github.com/NoahCLR/charybdis-4x6) builds
against. If you just want to use the keyboard, you don't need this repo:
download the firmware from its
[releases](https://github.com/NoahCLR/charybdis-4x6/releases/latest) and edit
it with [Charybdis Ark](https://github.com/NoahCLR/charybdis-ark).

## What it adds to QMK

- **The RP2040 Charybdis 4x6.** Upstream QMK only has the Elite-C and
  Blackpill versions, and [BastardKB's fork](https://github.com/Bastardkb/bastardkb-qmk)
  stopped at QMK 0.29. This fork carries the RP2040 board definition on QMK
  0.34.6, with the PMW3360 trackball, hi-res dragscroll and the RP2040
  double-tap bootloader. The other BastardKB boards are removed.
- **A few small hooks the firmware uses**:
  - the auto-mouse elapsed time, for the lighting that fades as the pointer
    layer is about to drop;
  - split activity hooks, so the halves share activity without flooding the
    link;
  - a CRC on every split serial frame, so a garbled message is refused;
  - the physical event queues and a record admission hook, for gesture timing.

Everything else, the firmware's own policy included, lives in the
[firmware repo](https://github.com/NoahCLR/charybdis-4x6). The patches here
stay small and hook-shaped, so syncing with upstream QMK stays cheap.

## Branches

| Branch | What it is |
| --- | --- |
| `noah-userspace-contracts-dev` | where work lands; the firmware pins a commit on it in its `qmk-pin.json` |
| `noah-userspace-contracts` | the released line; each firmware release moves it to the commit that release is built with |
| `main` | an old mirror of upstream; not used |

Changes reach `noah-userspace-contracts-dev` only through pull requests, from
branches named `<type>/<slug>` (`fix/`, `feat/`, `refactor/`, `docs/`, `chore/`),
merged as merge commits rather than squashed. A merge keeps the branch's own commits, so
a commit the firmware pinned while the change was in review is still on the
trunk afterwards, and an upstream sync keeps QMK's history.

### Syncing with upstream QMK

The [fork contracts](docs/noah-fork-contracts.md) list the patches and downstream
checks every update must retain, plus client compatibility considerations.

1. Branch `chore/sync-upstream-qmk-<version>` from
   `noah-userspace-contracts-dev` and merge the upstream release into it (a merge, never a rebase), resolving conflicts in favour of
   keeping the fork's hooks small.
2. Run `python3 util/check_noah_fork.py` and the audit checks described in
   the fork contracts. Then verify it the way every BK change is verified: through the firmware, whose
   host suite and pair build run against this branch.
3. Open its pull request and land it; it lands as a merge commit, so upstream's
   history stays intact.
4. The firmware picks it up in its own pull request, which re-pins
   `qmk-pin.json` to the landed commit (`sh tools/pin-qmk.sh` in the firmware).

## Building

Use it together with the firmware repo; its
[firmware guide](https://github.com/NoahCLR/charybdis-4x6/blob/main/docs/GUIDE.md#building-from-source)
has the details. The stock keymap still builds on its own:

```sh
qmk compile -kb bastardkb/charybdis/4x6 -km default
```

Developing it (pins, verification, releases) is described in the firmware's
[DEVELOPMENT.md](https://github.com/NoahCLR/charybdis-4x6/blob/main/docs/DEVELOPMENT.md).

Thanks to [QMK](https://docs.qmk.fm) and to
[BastardKB](https://bastardkb.com/) for the board and the original fork.
