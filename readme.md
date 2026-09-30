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
  0.32, with the PMW3360 trackball, hi-res dragscroll and the RP2040
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
| `noah-userspace-contracts` | the released line; it moves only when the whole stack is released |
| `main` | an old mirror of upstream; not used |

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
