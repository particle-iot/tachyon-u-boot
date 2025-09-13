// SPDX-License-Identifier: GPL-2.0+
/*
 * Memory layout parsing for Qualcomm.
 *
 * Copyright (c) 2025 Particle Industries, Inc.
 * Copyright (c) 2025 Linaro Limited.
 */

#include "qcom_dram.h"
#include <dm.h>
#include <log.h>
#include <sort.h>
#include <smem.h>
#include <dm/device-internal.h>

int qcom_parse_memory_smem(qcom_mem_bank* banks, size_t nbanks)
{
    size_t size;
    int i, j = 0, ret;
    struct smem_ram_ptable *ram_ptable;
    struct smem_ram_ptn *p;

    struct udevice *dev = NULL;

    if (banks == NULL) {
        return -EINVAL;
    }
    ret = uclass_first_device_err(UCLASS_SMEM, &dev);
    if (ret) {
        debug("failed to uclass_get_device() %d\n", ret);
        return ret;
    }

    ram_ptable = smem_get(dev, -1 /* any */, SMEM_USABLE_RAM_PARTITION_TABLE, &size);
    if (!ram_ptable) {
        debug("Failed to find SMEM partition.\n");
        return -ENODEV;
    }

    /* Check validy of RAM */
    for (i = 0; i < RAM_NUM_PART_ENTRIES && j < CONFIG_NR_DRAM_BANKS && j < nbanks; i++) {
        p = &ram_ptable->parts[i];
        if (p->category != CATEGORY_SDRAM || p->type != TYPE_SYSMEM)
            continue;
        if (!p->size && !p->start)
            break;

        banks[j].start = p->start;
        banks[j].size = p->size;
        j++;
    }

    if (j == CONFIG_NR_DRAM_BANKS) {
        log_err("SMEM: More than CONFIG_NR_DRAM_BANKS (%u) entries!", CONFIG_NR_DRAM_BANKS);
    }

    qcom_sort_memory_banks(banks, j);

    return j;
}
