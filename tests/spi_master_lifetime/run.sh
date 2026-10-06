#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
# Only headers are stubbed: both production .c files are compiled unchanged.
for header in gpio ch hal chibios_config matrix action action_util mousekey programmable_button host suspend led wait; do
    printf '#include "stubs.h"\n' > "$BUILD_DIR/$header.h"
done
for keep in 0 1; do
    for select in 0 1; do
        for mutex in 0 1; do
            "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
                -DMCU_RP -DSPI_KEEP_DRIVER_READY="$keep" \
                -DSPI_SELECT_MODE="$select" -DSPI_USE_MUTUAL_EXCLUSION="$mutex" \
                -I"$BUILD_DIR" -I"$ROOT/tests/spi_master_lifetime" -I"$ROOT/drivers" \
                "$ROOT/tests/spi_master_lifetime/test.c" \
                "$ROOT/platforms/chibios/drivers/spi_master.c" \
                "$ROOT/platforms/chibios/suspend.c" -o "$BUILD_DIR/test"
            "$BUILD_DIR/test"
        done
    done
done
