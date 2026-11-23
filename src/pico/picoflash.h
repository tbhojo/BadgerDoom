/*
 * Copyright (c) 20222 Graham Sanderson
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "pico.h"

#if BADGER_DOOM
#include "hardware/flash.h"

#define BADGER_SAVE_START_OFFSET 0x200000u
#define BADGER_SAVE_END_OFFSET   0x300000u
#else
#define FLASH_SECTOR_SIZE (1u << 12)
#endif

// erase and write a 4K sector
void picoflash_sector_program(uint32_t flash_offs, const uint8_t *data);
