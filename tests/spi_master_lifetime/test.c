// Compile the production SPI wrapper and suspend entry against a HAL spy.
// Counts establish driver lifecycle calls, not hardware timing or DMA cost.
#include "stubs.h"
#include "spi_master.h"
#include <stdio.h>
#include <string.h>

SPIDriver SPID2;
static unsigned starts, cold_starts, stops, bus_depth, acquires, releases;
static unsigned selects, unselects, outputs, transfers, suspend_hooks, wake_hooks;
static bool selected;
static bool pin_high[256];

void gpio_set_pin_input(pin_t pin) { (void)pin; }
void gpio_set_pin_output(pin_t pin) { (void)pin; outputs++; }
void gpio_write_pin(pin_t pin, bool high) { pin_high[pin] = high; }
void palSetPadMode(unsigned port, unsigned pad, unsigned mode) { (void)port; (void)pad; (void)mode; }
void chThdSleepMilliseconds(unsigned ms) { assert(ms == 10); }
void spiAcquireBus(SPIDriver *driver) { assert(driver == &SPID2); assert(!bus_depth); acquires++; bus_depth++; }
void spiReleaseBus(SPIDriver *driver) { assert(driver == &SPID2); assert(bus_depth); releases++; bus_depth--; }
int spiStart(SPIDriver *driver, const SPIConfig *config) {
    assert(!selected);
    assert(driver->state == SPI_STOP || driver->state == SPI_READY);
#if SPI_USE_MUTUAL_EXCLUSION
    assert(bus_depth == 1);
#endif
    starts++;
    cold_starts += driver->state == SPI_STOP;
    driver->state = SPI_READY;
    driver->config = config;
    return 0;
}
void spiStop(SPIDriver *driver) {
    assert(!selected);
    if (driver->state == SPI_READY) stops++;
    driver->state = SPI_STOP;
    driver->config = NULL;
}
void spiSelect(SPIDriver *driver) {
    assert(!selected && driver->state == SPI_READY);
    selected = true;
    selects++;
#if SPI_SELECT_MODE == SPI_SELECT_MODE_PAD
    pin_high[driver->config->sspad] = false;
#endif
}
void spiUnselect(SPIDriver *driver) {
    assert(selected && driver->state == SPI_READY);
    selected = false;
    unselects++;
#if SPI_SELECT_MODE == SPI_SELECT_MODE_PAD
    pin_high[driver->config->sspad] = true;
#endif
}
void spiExchange(SPIDriver *driver, size_t size, const void *tx, void *rx) {
    assert(selected && driver->state == SPI_READY);
    memcpy(rx, tx, size);
    transfers++;
}
void spiReceive(SPIDriver *driver, size_t size, void *rx) {
    assert(selected && driver->state == SPI_READY);
    memset(rx, 0x42, size);
    transfers++;
}
void spiSend(SPIDriver *driver, size_t size, const void *tx) {
    assert(selected && driver->state == SPI_READY && size && tx);
    transfers++;
}
void suspend_power_down_quantum(void) { suspend_hooks++; }
void suspend_wakeup_init_quantum(void) { wake_hooks++; }
void clear_mods(void) {}
void clear_weak_mods(void) {}
void clear_keys(void) {}
void wait_ms(unsigned ms) { assert(ms == 17); assert(!selected); }

static void check_idle(void) {
    assert(!selected && !bus_depth);
    assert(acquires == releases);
    assert(selects == unselects);
}

int main(void) {
    spi_init();
    assert(SPID2.state == SPI_STOP);
    // Suspend before first use must also be harmless.
    suspend_power_down();
    for (unsigned i = 0; i < 100; i++) {
        uint8_t burst[6];
        assert(spi_start(16, false, 3, 64));
        assert(!pin_high[16]);
        assert(SPID2.config->SSPCPSR == 64);
        assert(SPID2.config->SSPCR0 == (7 | SPI_SSPCR0_SPO | SPI_SSPCR0_SPH));
        assert(spi_write(0x50) == 0x50);
        assert(spi_receive(burst, sizeof(burst)) == SPI_STATUS_SUCCESS);
        assert(burst[0] == 0x42 && burst[5] == 0x42);
        spi_stop();
        assert(pin_high[16]);
        check_idle();
    }
    assert(starts == 100 && transfers == 200);
#if SPI_KEEP_DRIVER_READY
    assert(cold_starts == 1 && stops == 0 && SPID2.state == SPI_READY);
    spi_stop(); // A completed transaction must not release the bus twice.
    check_idle();
#else
    assert(cold_starts == 100 && stops == 100 && SPID2.state == SPI_STOP);
#endif
    spi_init(); // Repeated initialization must not disrupt retained state.
    assert(starts == 100);

    // Invalid configuration cannot assert CS, acquire a persistent transaction,
    // or modify the previously completed HAL state.
    unsigned previous_outputs = outputs;
    unsigned previous_starts = starts;
    unsigned previous_state = SPID2.state;
    assert(!spi_start(17, false, 1, 512));
#if SPI_SELECT_MODE == SPI_SELECT_MODE_PAD
    assert(!spi_start(NO_PIN, false, 1, 64));
#endif
    assert(starts == previous_starts && outputs == previous_outputs);
    assert(SPID2.state == previous_state);
    check_idle();

    // Another device still gets its requested mode/divisor/CS.
    assert(spi_start(17, false, 1, 100));
    assert(SPID2.config->SSPCPSR == 128);
    assert(SPID2.config->SSPCR0 == (7 | SPI_SSPCR0_SPH));
    assert(pin_high[16] && !pin_high[17]);
#if !SPI_USE_MUTUAL_EXCLUSION
    // With mutual exclusion off, the transaction guard rejects nesting. With
    // a nonrecursive bus mutex, clients must not attempt a nested acquisition.
    assert(!spi_start(18, false, 0, 2));
#endif
    assert(selected && SPID2.config->SSPCPSR == 128);
    assert(spi_read() == 0x42);
    uint8_t data[] = {1, 2};
    assert(spi_transmit(data, sizeof(data)) == SPI_STATUS_SUCCESS);
    spi_stop();
    assert(pin_high[17]);
    check_idle();

#if SPI_SELECT_MODE == SPI_SELECT_MODE_NONE
    spi_start_config_t active_high = {.slave_pin = 18, .mode = 2, .divisor = 32, .cs_active_low = false};
    assert(spi_start_extended(&active_high));
    assert(pin_high[18]);
    spi_stop();
    assert(!pin_high[18]);
    assert(spi_start(NO_PIN, false, 0, 2));
    spi_stop();
    check_idle();
#endif

    // Use the real suspend entry, including chaining the normal hooks.
    unsigned previous_cold_starts = cold_starts;
    suspend_power_down();
    assert(SPID2.state == SPI_STOP);
    suspend_power_down();
    assert(SPID2.state == SPI_STOP && suspend_hooks == 3);
    suspend_wakeup_init();
    assert(wake_hooks == 1);
    assert(spi_start(16, false, 3, 64));
    assert(cold_starts == previous_cold_starts + 1);
    spi_stop();
    check_idle();
#if SPI_KEEP_DRIVER_READY
    assert(SPID2.state == SPI_READY);
    spi_power_down();
    assert(SPID2.state == SPI_STOP);
#endif
    // A client using the HAL directly may stop the shared peripheral. The
    // wrapper must restart from the real HAL state, without a stale cache.
    assert(spi_start(16, false, 3, 64));
    spi_stop();
#if SPI_USE_MUTUAL_EXCLUSION
    spiAcquireBus(&SPID2);
#endif
    spiStop(&SPID2);
#if SPI_USE_MUTUAL_EXCLUSION
    spiReleaseBus(&SPID2);
#endif
    previous_cold_starts = cold_starts;
    assert(spi_start(17, false, 0, 16));
    assert(cold_starts == previous_cold_starts + 1);
    assert(SPID2.config->SSPCPSR == 16 && SPID2.config->SSPCR0 == 7);
    spi_stop();
    check_idle();
    puts("SPI transaction lifetime and suspend tests passed");
}
