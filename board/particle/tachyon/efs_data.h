// SPDX-License-Identifier: GPL-2.0+
/*
 * EFS parser
 *
 * Copyright (c) 2025 Particle Industries, Inc.
 */

#pragma once

#include <stdint.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#pragma pack(push, 1)
// Based on https://github.com/msm8916-mainline/qtestsign/blob/main/mbn/hashseg.py
// and https://github.com/coreboot/coreboot/blob/812d0e2f626dfea7e7deb960a8dc08ff0e026bc1/util/qualcomm/mbn_tools.py#L506-L691

typedef enum mbn_image_id {
    MBN_IMAGE_ID_NONE = 0,
    MBN_IMAGE_ID_OEM_SBL = 1,
    MBN_IMAGE_ID_AMSS = 2,
    MBN_IMAGE_ID_QCSBL = 3,
    MBN_IMAGE_ID_HASH = 4,
    MBN_IMAGE_ID_APPSBL = 5,
    MBN_IMAGE_ID_APPS = 6,
    MBN_IMAGE_ID_HOSTDL = 7,
    MBN_IMAGE_ID_DSP1 = 8,
    MBN_IMAGE_ID_FSBL = 9,
    MBN_IMAGE_ID_DBL = 10,
    MBN_IMAGE_ID_OSBL = 11,
    MBN_IMAGE_ID_DSP2 = 12,
    MBN_IMAGE_ID_EHOSTDL = 13,
    MBN_IMAGE_ID_NANDPRG = 14,
    MBN_IMAGE_ID_NORPRG = 15,
    MBN_IMAGE_ID_RAMFS1 = 16,
    MBN_IMAGE_ID_RAMFS2 = 17,
    MBN_IMAGE_ID_ADSP_Q5 = 18,
    MBN_IMAGE_ID_APPS_KERNEL = 19,
    MBN_IMAGE_ID_BACKUP_RAMFS = 20,
    MBN_IMAGE_ID_SBL1 = 21,
    MBN_IMAGE_ID_SBL2 = 22,
    MBN_IMAGE_ID_RPM = 23,
    MBN_IMAGE_ID_SBL3 = 24,
    MBN_IMAGE_ID_TZ = 25,
    MBN_IMAGE_ID_PSI = 32
} mbn_image_id;

typedef struct efs_boot_header {
    uint32_t image_id;
    uint32_t version;
    uint32_t image_src;
    uint32_t image_dst_ptr;
    uint32_t image_size;
    uint32_t code_size;
    uint32_t sig_ptr;
    uint32_t sig_size;
    uint32_t cert_chain_ptr;
    uint32_t cert_chain_size;
} efs_boot_header;

#define EFS_IMAGE_HEADER "IMGEFS"

typedef enum efs_image_type {
    EFS_IMAGE_TYPE_GOLD = 'G',
    EFS_IMAGE_TYPE_MODEM_1 = '1',
    EFS_IMAGE_TYPE_MODEM_2 = '2'
} efs_image_type;

typedef struct efs_image_header {
    uint8_t magic[6]; // "IMGEFS"
    uint8_t type; // "G" for fsg, 1 and 2 for modemst1/2 from the looks of it
    uint32_t unk1;
    uint32_t unk2;
    uint32_t unk3;
    uint8_t unk4;
    uint8_t unk5;
} efs_image_header;

typedef struct efs_superblock_data {
    uint32_t start_sector; // probably
    uint32_t end_sector; // probably
    uint32_t size; // seems to almost match the size of (end_sector - start_sector)*sector_size,
                   // slightly larger
    uint8_t padding[57]; // 0xff padded
} efs_superblock_data;

_Static_assert(sizeof(efs_superblock_data) == 69, "efs_superblock_data must be 69 bytes");

#define EFS_SUPER_BLOCK_HEADER (0x53000000)
#define EFS_SUPER_BLOCK_MAGIC "EFSSuper"

#define EFS_INVALID_SECTOR (0xffffffff)
#define EFS_INVALID_INODE (0xffffffff)

typedef struct efs_superblock {
    uint32_t header; // 0x53000000 little-endian - 'S'
    uint32_t version;
    uint8_t magic[8]; // "EFSSuper"
    uint8_t type; // could be a revision
    uint8_t unk1;
    uint32_t unk2;
    uint32_t sector_size; // 512
    uint32_t sector_count; // sector_size * sector_count = EFS size
    uint32_t sector_size1; // seems to be the same value as the one above
    uint32_t sectors_per_block; // seen 2
    uint32_t unk3; // 0x00000000

    efs_superblock_data dummy; // all 0xffffffff but seems to be same size as 'data'
    efs_superblock_data data; // has valid info

    uint32_t upper_data[32]; // Looks to be some sector numbers, called upper_data in some places
    uint32_t unk5[2]; // 0x0
    uint32_t unk6; // 0xfffffffe
    uint8_t unk7; // 0x00
    uint8_t padding[95]; // 0xff
    // Up until the end of page all 0x00, not 0xff, so might be some additional stuff in there
    // but we don't care about it
} efs_superblock;

typedef enum efs_upper_data_index {
    EFS_UPPER_DB_ROOT = 2
} efs_upper_data_index;

#define EFS_GROUP_DESCRIPTOR_HEADER (0xa7b93ea0)

typedef struct efs_group_descriptor {
    uint32_t header; // EFS_GROUP_DESCRIPTOR_HEADER
    uint32_t version;
    uint32_t inode_top;
    uint32_t inode_next;
    uint32_t inode_free;
    uint32_t inode_root;
} efs_group_descriptor;

typedef struct efs_node {
    uint32_t prev;
    uint32_t next;
    uint16_t used;
    uint16_t padding; // 0xff
    uint32_t gid;
    uint8_t unk1;
    uint8_t unk2; // always 0
} efs_node;

#define EFS_DIR_ENTRY_TYPE 'd'

typedef struct efs_dir_entry {
    uint8_t size;
    uint8_t inode_item_size;
    uint8_t type; // always 'd'
    uint32_t parent_inode;
    char name[];
} efs_dir_entry;

typedef enum efs_inode_item_type {
    EFS_INODE_ITEM_REF = 'i', // just uint32_t
    EFS_INODE_ITEM_INLINE = 'n', // efs_inode_inline
    EFS_INODE_ITEM_INLINE_EXT = 'N', // efs_inode_inline_ext
} efs_inode_item_type;

typedef struct efs_inode_item {
    uint8_t type; // efs_inode_item_type
    char data[];
} efs_inode_item;

typedef struct efs_inode_item_ref {
    uint32_t ref;
} efs_inode_item_ref;

typedef struct efs_inode_item_inline {
    uint16_t mode; // POSIX
    char data[];
} efs_inode_item_inline;

typedef struct efs_inode_item_inline_ext {
    uint16_t mode; // POSIX
    uint16_t gid;
    uint32_t ctime;
    char data[];
} efs_inode_item_inline_ext;

#define EFS_INODE_INDEX_BITS (2)

// Looks to be similar to ext2 inode structure
typedef struct efs_inode {
    uint16_t mode; // POSIX
    uint16_t unk1;
    uint32_t unk2;
    uint32_t size;
    uint16_t uid;
    uint16_t gid;
    uint32_t unk3;
    uint32_t blocks;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t atime;
    uint32_t padding[7]; // 00 filled
    uint32_t sectors[13]; // direct
    uint32_t sectors_indirect[3];
    // ^^ point to a sector that just contains array of uint32_t sector numbers
} efs_inode;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif // __cplusplus
