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
  dual-role keys; `process_record_admit_user` runs before quantum processing
  and lets downstream defer/replay records.
- `quantum/action_tapping.{c,h}` and
  `quantum/process_keycode/process_combo.{c,h}`: read-only physical-event
  queue queries preserve gesture timing while records wait inside QMK.

The 0.34.6 merge removes or replaces none of these contracts. The retained
keyboard tree, auto-mouse files, serial protocol and split transaction files
are unchanged from the pre-update fork. The custom gesture function bodies
are also unchanged; upstream tapping/combo changes remain around them.

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
