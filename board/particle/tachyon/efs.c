// SPDX-License-Identifier: GPL-2.0+
/*
 * EFS parser
 *
 * Copyright (c) 2025 Particle Industries, Inc.
 */

#include "efs.h"
#include "efs_impl.h"
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include <linux/stat.h>
#include <linux/kernel.h>

int efs_mount(efs_context* ctx, efs_ops ops) {
    EFS_LOG(INFO, ctx, "Trying to mount EFS filesystem");

    memset(ctx, 0, sizeof(efs_context));
    memcpy(&ctx->ops, &ops, sizeof(efs_ops));
    
    loff_t pos = 0;

    efs_boot_header boot = {};
    EFS_CHECK(ctx->ops.read(&boot, pos, sizeof(boot), ctx->ops.ctx));
    // We've only seen BACKUP_RAMFS image id in 'fsg' partition on our devices
    if (boot.image_id != MBN_IMAGE_ID_BACKUP_RAMFS) {
        EFS_LOG(ERROR, ctx, "Unexpected image_id = %08x", boot.image_id);
        return EFS_ERROR_GENERIC;
    }

    pos += sizeof(boot);

    efs_image_header header = {};
    EFS_CHECK(ctx->ops.read(&header, pos, sizeof(header), ctx->ops.ctx));
    if (strncmp((const char*)header.magic, EFS_IMAGE_HEADER, sizeof(header.magic)) || header.type != EFS_IMAGE_TYPE_GOLD) {
        EFS_LOG(ERROR, ctx, "Unexpected EFS image header magic (%08x) or type (%02x)", header.magic, header.type);
        return EFS_ERROR_GENERIC;
    }

    pos += sizeof(header);

    efs_superblock superblock = {};
    pos = EFS_CHECK(efs_find_superblock(ctx, &superblock, pos, boot.image_size));

    EFS_LOG(DEBUG, ctx, "EFS Superblock @ %08x: version=%lu type=%02x sector_size=%lu sector_count=%lu sectors_per_block=%lu root=%lu",
            pos,
            (unsigned)superblock.version,
            superblock.type,
            superblock.sector_size,
            superblock.sector_count,
            superblock.sectors_per_block,
            superblock.upper_data[EFS_UPPER_DB_ROOT]);

    ctx->superblock_offset = pos;

    ctx->sector_size = superblock.sector_size;
    ctx->sector_count = superblock.sector_count;
    ctx->sectors_per_block = superblock.sectors_per_block;
    ctx->database_root = superblock.upper_data[EFS_UPPER_DB_ROOT];

    efs_group_descriptor desc = {};
    EFS_CHECK(efs_find_group_descriptor(ctx, &desc));

    ctx->inode_root = desc.inode_root;

    EFS_LOG(DEBUG, ctx, "EFS group descriptor: version=%lu inode_top=%08x inode_next=%08x inode_free=%08x inode_root=%08x",
            desc.version,
            desc.inode_top,
            desc.inode_next,
            desc.inode_free,
            desc.inode_root);

    return 0;
}

int efs_unmount(efs_context* ctx) {
    return 0;
}

int efs_open(efs_context* ctx, efs_file** file, const char* path, int flags) {
    EFS_CHECK_TRUE(ctx, EFS_ERROR_GENERIC);
    EFS_CHECK_TRUE(file, EFS_ERROR_GENERIC);

    // Check that path exists
    efs_iterator it = {};
    efs_iterator_root(ctx, &it);
    EFS_CHECK(efs_resolve_path(ctx, path, &it));
    EFS_CHECK_TRUE(!efs_dir_entry_is_directory(ctx, efs_iterator_dir_entry(&it)), EFS_ERROR_GENERIC);

    *file = (efs_file*)calloc(sizeof(efs_file_impl), 1);
    EFS_CHECK_TRUE(*file, EFS_ERROR_GENERIC);

    efs_file_impl* impl = (efs_file_impl*)*file;
    impl->it = it;
    efs_inode_item* item = efs_dir_entry_inode_item(efs_iterator_dir_entry(&it));
    if (item->type == EFS_INODE_ITEM_REF) {
        EFS_CHECK(efs_read_inode(ctx, &impl->inode_it.inode, efs_dir_entry_inode_ref(efs_iterator_dir_entry(&it))));
    } else if (item->type == EFS_INODE_ITEM_INLINE) {
        memcpy(&impl->inode_it.inln, item->data, sizeof(impl->inode_it.inln) + efs_inode_item_data_size(efs_iterator_dir_entry(&impl->it)));
    } else if (item->type == EFS_INODE_ITEM_INLINE_EXT) {
        memcpy(&impl->inode_it.inln_ext, item->data, sizeof(impl->inode_it.inln_ext) + efs_inode_item_data_size(efs_iterator_dir_entry(&impl->it)));
    } else {
        free(file);
        return EFS_ERROR_GENERIC;
    }
    return 0;
}

int efs_stat(efs_context* ctx, const char* path, efs_dirent* ent) {
    EFS_CHECK_TRUE(ctx, EFS_ERROR_GENERIC);

    // Check that path exists
    efs_iterator it = {};
    efs_iterator_root(ctx, &it);
    EFS_CHECK(efs_resolve_path(ctx, path, &it));

    if (ent) {
        size_t name_len = 0;
        const char* name = efs_dir_entry_name(efs_iterator_dir_entry(&it), &name_len);
        strncpy(ent->name, name, name_len);

        ent->size = efs_inode_size(ctx, efs_iterator_dir_entry(&it));

        if (efs_dir_entry_is_directory(ctx, efs_iterator_dir_entry(&it))) {
            ent->type = EFS_TYPE_DIR;
        } else {
            ent->type = EFS_TYPE_FILE;
        }
    }
    return 0;
}

int efs_read(efs_context* ctx, efs_file* file, void* buffer, loff_t offset, loff_t size) {
    efs_file_impl* impl = (efs_file_impl*)file;
    efs_inode_item* item = efs_dir_entry_inode_item(efs_iterator_dir_entry(&impl->it));
    if (item->type == EFS_INODE_ITEM_REF) {
        return efs_inode_read(ctx, &impl->inode_it, buffer, offset, size);
    } else if (item->type == EFS_INODE_ITEM_INLINE) {
        offset = min(offset, efs_inode_size(ctx, efs_iterator_dir_entry(&impl->it)));
        size = min(size, efs_inode_size(ctx, efs_iterator_dir_entry(&impl->it)) - offset);
        memcpy(buffer, impl->inode_it.inln.data + offset, size);
        return size;
    } else if (item->type == EFS_INODE_ITEM_INLINE_EXT) {
        offset = min(offset, efs_inode_size(ctx, efs_iterator_dir_entry(&impl->it)));
        size = min(size, efs_inode_size(ctx, efs_iterator_dir_entry(&impl->it)) - offset);
        memcpy(buffer, impl->inode_it.inln_ext.data + offset, size);
        return size;
    }
    return EFS_ERROR_GENERIC;
}

int efs_close(efs_context* ctx, efs_file* file) {
    EFS_CHECK_TRUE(file, EFS_ERROR_GENERIC);
    free(file);
    return 0;
}

// TODO: based on efs_traverse
int efs_opendir(efs_context* ctx, efs_dir** dir, const char* path);
int efs_readdir(efs_context* ctx, efs_dir* dir, efs_dirent* ent);
int efs_closedir(efs_context* ctx, efs_dir* dir);
