// Board definition for the Scopocket v1.3 PCB.
// RP2040 + W25Q128JVSIQ (16 MB QSPI flash) + 12 MHz crystal.

#ifndef _BOARDS_SCOPOCKET_H
#define _BOARDS_SCOPOCKET_H

#define SCOPOCKET_V1_3

// No UART / I2C / LED defaults: every GPIO on this board has a dedicated job.

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// Give the crystal extra time to start up on a custom board.
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 1
#endif

#endif
