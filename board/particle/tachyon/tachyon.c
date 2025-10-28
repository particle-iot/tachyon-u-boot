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
#include <blk.h>
#include <part.h>
#include <scsi.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <memalign.h>
#include <net-common.h>

#include "efs.h"

DECLARE_GLOBAL_DATA_PTR;

static efs_context s_efs = {};
typedef struct blkdev_context {
	struct blk_desc* desc;
	struct disk_partition info;
	bool mounted;
} blkdev_context;
blkdev_context s_efs_blk = {};

#define TACHYON_FSG_PARTITION_NAME "fsg"
#define TACHYON_FSG_WIFI_MAC_PATH "/nvm/num/4678"
#define TACHYON_FSG_BLUETOOTH_MAC_PATH "/nvm/num/447"

// #define DEBUG

// #ifdef DEBUG
// #undef debug
// #define debug(fmt, args...) do { printf(fmt, ##args); } while (0)
// #endif // DEBUG

#define CHECK(_expr) \
    ({ \
        const typeof(_expr) _ret = _expr; \
        if (_ret < 0) { \
            return _ret; \
        } \
        _ret; \
    })

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

loff_t fsg_read(void* buf, loff_t offset, loff_t size, void* ctx) {
    blkdev_context* blk = (blkdev_context*)ctx;

	if (size <= 0) {
		return 0;
	}

	// Just one block should be fine, EFS driver won't request more than
	// the underlying EFS block size anyway
	size_t block_size = blk->desc->blksz;

	loff_t start_block = offset / block_size;
	loff_t skip_bytes = offset % block_size;
	loff_t block_count = DIV_ROUND_UP(size + skip_bytes, block_size);

	u8* block_buf = memalign(ARCH_DMA_MINALIGN, block_size * block_count);
	if (!block_buf) {
		return -ENOMEM;
	}

    if (blk_dread(blk->desc, blk->info.start + start_block, block_count, block_buf) == block_count) {
		memcpy(buf, block_buf + skip_bytes, size);
	} else {
		size = -1;
	}
	free(block_buf);
	return size;
}

int efs_logger(efs_loglevel level, void* ctx, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    int r = vprintf(fmt, args);
    printf("\n");
    va_end(args);
    return r;
}

static int tachyon_setup_efs(void) {
	bool mounted = s_efs_blk.mounted;
	struct udevice* dev = NULL;
	struct blk_desc* desc = NULL;
	struct disk_partition info = {};
	int devnum = -1;
	int partnum = -1;

	s_efs_blk.mounted = 0;

	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		if (device_get_uclass_id(dev) != UCLASS_BLK) {
			continue;
		}

		desc = dev_get_uclass_plat(dev);
		if (!desc || desc->part_type == PART_TYPE_UNKNOWN) {
			continue;
		}
		devnum = desc->devnum;
		partnum = part_get_info_by_name(desc, TACHYON_FSG_PARTITION_NAME, &info);

		if (partnum >= 0) {
			printf("Found 'fsg' parition %d:%d block size=%lu/%lu\n", devnum, partnum, desc->blksz, info.blksz);
			break;
		}
	}

	CHECK(devnum);
	CHECK(partnum);

	s_efs_blk.info = info;
	s_efs_blk.desc = desc;

	if (mounted) {
		s_efs_blk.mounted = mounted;
		return 0;
	}

	efs_ops ops = {
        .ctx = &s_efs_blk,
        .read = fsg_read,
        .logger = efs_logger
    };

    int r = efs_mount(&s_efs, ops);
	s_efs_blk.mounted = r == 0;
	return r;
}

static int efs_read_file(const char* filename, void* buffer, loff_t size) {
	efs_file* f = NULL;
	CHECK(efs_open(&s_efs, &f, filename, 0));
    int r = efs_read(&s_efs, f, buffer, 0, size);
	efs_close(&s_efs, f);
	return r;
}

int qcom_late_init(void)
{
	// TODO: serial# env?
	return 0;
}

int ft_system_setup(void *fdt, struct bd_info *bd)
{
	CHECK(tachyon_setup_efs());

	u8 mac[ARP_HLEN] = {};
	if (!eth_env_get_enetaddr("wlanaddr", mac)) {
		if (efs_read_file(TACHYON_FSG_WIFI_MAC_PATH, mac, ARP_HLEN) == ARP_HLEN) {
			printf("Found WiFi MAC address in FSG: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			const char* path = fdt_get_alias(fdt, "wlan");
			if (path) {
				do_fixup_by_path(fdt, path, "local-mac-address", mac, ARP_HLEN, 1);
				do_fixup_by_path(fdt, path, "mac-address", mac, ARP_HLEN, 1);
				printf("WiFi MAC fixed up in %s\n", path);
			}
		}
	}

	if (!eth_env_get_enetaddr("btaddr", mac)) {
		if (efs_read_file(TACHYON_FSG_BLUETOOTH_MAC_PATH, mac, ARP_HLEN) == ARP_HLEN) {
			// Bluetooth MAC is stored in reverse
			u8 reversed[ARP_HLEN] = {};
			for (int i = 0; i < sizeof(mac); i++) {
				reversed[i] = mac[sizeof(mac) - 1 - i];
			}
			printf("Found Bluetooth MAC address in FSG: %02x:%02x:%02x:%02x:%02x:%02x\n",
					reversed[0], reversed[1], reversed[2], reversed[3], reversed[4], reversed[5]);
			const char* path = fdt_get_alias(fdt, "bluetooth");
			if (path) {
				do_fixup_by_path(fdt, path, "local-bd-address", mac, ARP_HLEN, 1);
				printf("Bluetooth MAC fixed up in %s\n", path);
			}
		}
	}

	return 0;
}
