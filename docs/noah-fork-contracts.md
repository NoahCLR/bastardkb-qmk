# Contracts retained across upstream QMK updates

This fork tracks upstream QMK 0.34.6 while retaining the RP2040 Charybdis
4x6 and the hooks required by Noah's downstream firmware. The upstream tag
is merged into the fork, preserving both histories. Upstream keyboard
additions are excluded: the retained keyboard tree remains Charybdis-only.
Disable rename detection for this merge (`git -c merge.renames=false merge`):
identical boilerplate in deleted keyboard headers otherwise produces false
renames into unrelated core files and our board definition. Review the
retained board tree separately rather than accepting inferred renames.

## Fork-owned contracts

- `keyboards/bastardkb/`: RP2040 4x6 matrix, pins, PMW3360 integration,
  high-resolution drag-scroll, RGB mapping and double-tap bootloader.
- `quantum/pointing_device/pointing_device_auto_mouse.{c,h}`:
  `auto_mouse_get_time_elapsed_at` exposes wrap-safe elapsed time using the
  caller's sampled clock; the original wrapper delegates to it.
- `quantum/split_common/transactions.{c,h}`: activity admission sees the
  latest snapshot and last successful send; forced repair bypasses admission.
  Optional transaction timing and CRC diagnostics remain available.
- `platforms/chibios/drivers/serial_protocol.c` and the transaction layer:
  optional CRC8 frames, incompatible-pair handshake rejection, bounded
  staging, dropped-write repair, and checked RPC sequence admission. Corrupt
  frames cannot publish shared data or execute stale requests.
- `quantum/action.{c,h}`: `is_tap_keycode_user` lets downstream classify its
  dual-role keys, and `is_tap_record_user` has the final say with the record,
  QMK's own answer and the keycode hook's (its default keeps the keycode
  hook's), for ownership that depends on where a key was pressed; `process_record_admit_user` runs before quantum processing
  and lets downstream defer/replay records. `KEYRECORD_USER_DATA` adds one
  optional opaque byte to `keyrecord_t`, copied by all queues and by synthesized
  tapping releases; its interpretation remains downstream policy.
- `quantum/action_tapping.{c,h}` and
  `quantum/process_keycode/process_combo.{c,h}`: read-only physical-event
  queue queries preserve gesture timing while records wait inside QMK.
  With `COMBO_KEY_RECORD_FILTER`, weak `combo_key_record_allowed` defaults true;
  a downstream veto skips every member state mutation, on presses and releases.
  This record gate is distinct from the upstream trigger veto, whose rejected
  press/release may still update constituent state. Without the feature flag,
  the upstream path is unchanged.

The 0.34.6 merge removes or replaces none of these contracts. The retained
keyboard tree, auto-mouse files, serial protocol and split transaction files
are unchanged from the pre-update fork. The custom gesture function bodies
are also unchanged; upstream tapping/combo changes remain around them.

## Report-only modifier override

`quantum/action_util.{c,h}` exposes weak
`keyboard_report_mods_override_user(uint8_t *mods)`. Returning false preserves
upstream behavior. Returning true supplies the exact modifier byte for both
6KRO and NKRO, before change detection, and skips normal composition and
one-shot consumption for that report. Stored real, weak, one-shot and
speculative modifier state is unchanged. Downstream must send a report when
entering, changing or leaving its override. The hook must not recurse into
report sending or alter ownership. Host-input policy belongs downstream.

## Upstream changes relevant to the downstream firmware

- Mouse/wheel report minima now match the HID descriptor (-127/-32767).
- Reactive RGB last-hit history uses `memmove` for overlapping copies.
- VIA reports protocol 13 and supports the keycode-version query. Downstream
  clients must accept that version before testing live editing; clients that
  require exactly protocol 12 will reject it. This does not itself change the
  custom Profile Wire protocol, its storage geometry or its action ABI.
- Persistent community-module data and `eeconfig_prepare_datablocks` add
  initialization paths. The current downstream firmware does not allocate
  community-module, keyboard or user datablocks; its own storage and recovery
  stay in downstream userspace. Review this again if those allocations change.
- ChibiOS/ChibiOS-Contrib advance to upstream's pins; Pico SDK is unchanged.
  Upstream removes deprecated `isLeftHand` and `FORCE_NKRO`; the retained board
  and current downstream source use neither.

Keep compiler adoption separate: this update uses the downstream pinned build
image. It does not include unlanded performance experiments from other branches.

## Verification ownership

The downstream firmware owns executable coverage of these contracts. Set
`QMK_ROOT` to this worktree when running its host suite. In particular:

- `run_qmk_contract_checks.sh`: hook placement, queue APIs, sampled auto-mouse
  clock and Raw HID endpoint size;
- `run_qmk_gesture_pipeline_tests.sh`: real upstream tapping/combo interaction;
- `run_split_activity_tests.sh`, `run_split_frame_crc_tests.sh`,
  `run_split_transport_build_tests.sh`: admission, corrupt/mixed frames,
  repair and compile guards;
- `run_qmk_physical_half_tests.sh`, `run_runtime_init_order_tests.sh` and the
  VIA/profile storage runners: identity, initialization and recovery;
- `run_feature_gate_compile_tests.sh`: conditional integration boundaries.

The vault's `verify` runs the full firmware host suite and builds both physical
halves with the pinned compiler. It records the exact source/dependency tree.
Host tests and compilation do not establish hardware polling rate, interruption
recovery or client acceptance. The trial pair must be tested before those are
claimed. A later landing adopts the published BK commit through a separate
firmware pin pull request; this branch alone does not change released firmware.

## Machine-checkable patch inventory

`noah-fork-patches.json` gives each patch a purpose, exact file list and
firmware regression runners. Its upstream commit is the comparison anchor;
that commit must exist locally and be an ancestor of HEAD. The checker
includes committed, staged and unstaged tracked changes plus nonignored
untracked paths. Stage new inventoried files before auditing them.

```sh
python3 util/check_noah_fork.py
python3 -m unittest discover -s util -p test_noah_fork.py
python3 util/check_noah_fork.py --firmware /absolute/path/to/charybdis-4x6 --run-tests
```

Run these before the normal vault `verify`. The audit is a required local
checkpoint in this repository's agent instructions; it is not injected into
upstream QMK's build or the shared vault tooling. The final command validates
that each referenced runner exists, sets `QMK_ROOT` to this worktree and runs
each runner once. It does not replace the full host suite or flashable pair.

Runtime and board entries fingerprint the reviewed binary-capable Git patch
against upstream, including file modes and full blob identities. Removing a
hook, restoring upstream's implementation, changing another part of the same
file, or losing a board file fails the audit. This is deliberately stricter
than checking that a function name still appears. It proves the reviewed
patch is intact, not that its behavior is correct; the host tests provide
behavioral coverage and hardware acceptance remains separate.

The only bulk exclusion allows deletions of unlisted keyboard files. New or
restored keyboard files require an explicit entry, even when restored bytes
match upstream exactly. Retained board files have their own fingerprint.
Documentation, workspace settings and audit tooling have exact named
maintenance entries without fingerprints. That avoids self-referential hashes
and keeps routine documentation edits independent of runtime review; these
files still require ordinary review and checker tests.

### Reviewing an intentional patch change

The audit prints the actual SHA-256 for every runtime/board entry, exits with
failure on a mismatch, and never writes the manifest or source. Inspect the
whole patch before changing a fingerprint:

```sh
python3 util/check_noah_fork.py --show-patch gesture-admission
```

When a patch changes intentionally, review the displayed delta, preserve or
adapt its behavioral tests, and copy the reviewed fingerprint into the
manifest in the same commit. New source differences require exact file
ownership and a purpose. Do not add a wildcard exclusion to make a check pass.
When upstream incorporates a patch, explicitly retire/adapt its inventory
entry and retain behavioral coverage as needed; do not silently drop it.

After merging a newer upstream release, update the manifest's comparison
anchor and review every changed fingerprint. Blob identities can change even
when our own hunk is unchanged: upstream edited the surrounding file, which
requires compatibility review. The report separates those runtime deltas from
the large count of intentionally excluded keyboards.
