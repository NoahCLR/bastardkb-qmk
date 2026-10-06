#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRUE 1
#define FALSE 0
#define NO_PIN 255
#define B13 13
#define B14 14
#define B15 15
#define SPI_SCK_FLAGS 0
#define SPI_MOSI_FLAGS 0
#define SPI_MISO_FLAGS 0
#define PAL_PORT(pin) 0
#define PAL_PAD(pin) (pin)
#define SPI_SELECT_MODE_NONE 0
#define SPI_SELECT_MODE_PAD 1
#define SPI_SSPCR0_FRF_MOTOROLA 0
#define SPI_SSPCR0_DSS_8BIT 7
#define SPI_SSPCR0_SPO (1u << 6)
#define SPI_SSPCR0_SPH (1u << 7)
#define SPI_STOP 0
#define SPI_READY 1
#define osalDbgAssert(condition, message) assert(condition)

typedef uint8_t pin_t;
typedef struct {
    uint32_t SSPCR0;
    uint16_t SSPCPSR;
    unsigned ssport;
    unsigned sspad;
} SPIConfig;
typedef struct {
    unsigned state;
    const SPIConfig *config;
} SPIDriver;
extern SPIDriver SPID2;

void gpio_set_pin_input(pin_t pin);
void gpio_set_pin_output(pin_t pin);
void gpio_write_pin(pin_t pin, bool high);
void palSetPadMode(unsigned port, unsigned pad, unsigned mode);
void chThdSleepMilliseconds(unsigned ms);
void spiAcquireBus(SPIDriver *driver);
void spiReleaseBus(SPIDriver *driver);
int spiStart(SPIDriver *driver, const SPIConfig *config);
void spiStop(SPIDriver *driver);
void spiSelect(SPIDriver *driver);
void spiUnselect(SPIDriver *driver);
void spiExchange(SPIDriver *driver, size_t size, const void *tx, void *rx);
void spiReceive(SPIDriver *driver, size_t size, void *rx);
void spiSend(SPIDriver *driver, size_t size, const void *tx);
void suspend_power_down_quantum(void);
void suspend_wakeup_init_quantum(void);
void clear_mods(void);
void clear_weak_mods(void);
void clear_keys(void);
void wait_ms(unsigned ms);
void suspend_power_down(void);
void suspend_wakeup_init(void);
