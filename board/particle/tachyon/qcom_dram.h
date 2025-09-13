// SPDX-License-Identifier: GPL-2.0+
/*
 * Memory layout parsing for Qualcomm.
 *
 * Copyright (c) 2025 Particle Industries, Inc.
 * Copyright (c) 2025 Linaro Limited.
 */

#pragma once

#include <asm-generic/unaligned.h>
#include "../../../arch/arm/mach-snapdragon/qcom-priv.h"
#include <stddef.h>

#define SMEM_USABLE_RAM_PARTITION_TABLE 402
#define RAM_PART_NAME_LENGTH            16
#define RAM_NUM_PART_ENTRIES            32
#define CATEGORY_SDRAM 0x0E
#define TYPE_SYSMEM 0x01

struct smem_ram_ptable_hdr {
	u32 magic[2];
	u32 version;
	u32 reserved;
	u32 len;
} __attribute__ ((__packed__));

struct smem_ram_ptn {
	char name[RAM_PART_NAME_LENGTH];
	u64 start;
	u64 size;
	u32 attr;
	u32 category;
	u32 domain;
	u32 type;
	u32 num_partitions;
	u32 reserved[3];
	u32 reserved2[2]; /* The struct grew by 8 bytes at some point */
} __attribute__ ((__packed__));

struct smem_ram_ptable {
	struct smem_ram_ptable_hdr hdr;
	u32 reserved;     /* Added for 8 bytes alignment of header */
	struct smem_ram_ptn parts[RAM_NUM_PART_ENTRIES];
} __attribute__ ((__packed__));

int qcom_parse_memory_smem(qcom_mem_bank* banks, size_t size);
