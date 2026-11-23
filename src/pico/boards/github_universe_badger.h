/* SPDX-License-Identifier: BSD-3-Clause */

// GitHub Universe 2024 badge, not the later Badgeware Badger 2350.
// Pinout: https://github.com/pimoroni/badger2040/blob/6ea88e080bc48350eb48c6608f67c18497709a31/firmware/PIMORONI_BADGER2350/pimoroni_badger2350.h
// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_GITHUB_UNIVERSE_BADGER_H
#define _BOARDS_GITHUB_UNIVERSE_BADGER_H

#define GITHUB_UNIVERSE_BADGER 1
#define PICO_RP2350A 1

#define BADGER_POWER_ENABLE_PIN 10
#define BADGER_BUTTON_DOWN_PIN 11
#define BADGER_BUTTON_A_PIN 12
#define BADGER_BUTTON_B_PIN 13
#define BADGER_BUTTON_C_PIN 14
#define BADGER_BUTTON_UP_PIN 15

#define BADGER_PANEL_SPI 0
#define BADGER_PANEL_CS_PIN 17
#define BADGER_PANEL_SCK_PIN 18
#define BADGER_PANEL_MOSI_PIN 19
#define BADGER_PANEL_DC_PIN 20
#define BADGER_PANEL_RESET_PIN 21
#define BADGER_PANEL_BUSY_PIN 26

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI BADGER_PANEL_SPI
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN BADGER_PANEL_CS_PIN
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN BADGER_PANEL_SCK_PIN
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN BADGER_PANEL_MOSI_PIN
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// The shipping firmware identifies a 4 MB board; picotool confirmed 4 MiB
// on the physical badge despite the 2 MiB declaration in its SDK header.
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (4 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
