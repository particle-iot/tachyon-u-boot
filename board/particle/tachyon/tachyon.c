// SPDX-License-Identifier: GPL-2.0+
/*
 * Board init file for Particle Tachyon
 *
 * (C) Copyright 2025 Particle Industries, Inc.
 */


#include <asm/global_data.h>
#include <log.h>
#include "qcom_dram.h"
#include <fdt_support.h>
#include <init.h>
#include <stdbool.h>
#include <dm/util.h>

DECLARE_GLOBAL_DATA_PTR;

// #define DEBUG

// #ifdef DEBUG
// #undef debug
// #define debug(fmt, args...) do { printf(fmt, ##args); } while (0)
// #endif // DEBUG

int board_early_init_f(void)
{
	qcom_mem_bank banks[CONFIG_NR_DRAM_BANKS] = {};
	int num = qcom_parse_memory_smem(banks, CONFIG_NR_DRAM_BANKS);

	if (num > 0) {
		memset(qcom_get_memory_banks(), 0, sizeof(qcom_mem_bank) * CONFIG_NR_DRAM_BANKS);
		memcpy(qcom_get_memory_banks(), banks, sizeof(qcom_mem_bank) * num);

		phys_size_t ram_end = 0;
		for (int i = 0; i < num; i++) {
			ram_end = max(ram_end, banks[i].start + banks[i].size);
		}
		gd->ram_base = banks[0].start;
		gd->ram_size = ram_end - gd->ram_base;
	}

	return 0;
}
