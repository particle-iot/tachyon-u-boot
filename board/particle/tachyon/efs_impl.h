// SPDX-License-Identifier: GPL-2.0+
/*
 * EFS parser
 *
 * Copyright (c) 2025 Particle Industries, Inc.
 */

#pragma once

#include <stdint.h>
#include <linux/types.h>
#include "efs_data.h"

#define EFS_LOG_ENABLED 1

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#if defined(EFS_LOG_ENABLED) && EFS_LOG_ENABLED

#define EFS_LOG(_level, _ctx, _fmt, ...) \
    ({ \
        if (_ctx->ops.logger) { \
            _ctx->ops.logger(EFS_LOG_##_level, _ctx->ops.ctx, _fmt, ##__VA_ARGS__); \
        } \
    })

#else
#define EFS_LOG(...)
#endif

#define EFS_CHECK(_expr) \
    ({ \
        const typeof(_expr) _ret = _expr; \
        if (_ret < 0) { \
            return _ret; \
        } \
        _ret; \
    })

#define EFS_CHECK_TRUE(_expr, _ret) \
    ({ \
        const bool _ok = (bool)(_expr); \
        if (!_ok) { \
            return _ret; \
        } \
        _ok; \
    })

typedef struct efs_iterator {
    size_t sector;
    efs_node node;

    loff_t pos; // position within the sector   
    char* dirent[sizeof(efs_dir_entry) + EFS_FILE_NAME_MAX_LEN + sizeof(efs_inode_item) + EFS_FILE_NAME_MAX_LEN];
} efs_iterator;

typedef struct efs_inode_iterator {
    union {
        efs_inode inode;
        efs_inode_item_inline inln;
        efs_inode_item_inline_ext inln_ext;
    };
} efs_inode_iterator;

typedef struct efs_path {
    size_t path_pos;
    char path[EFS_MAX_PATH];
} efs_path;

typedef enum efs_find_type {
    EFS_FIND_TYPE_SELF = 1, // .
    EFS_FIND_TYPE_PARENT = 2, // ..
    EFS_FIND_TYPE_CHILD = 4,
    EFS_FIND_TYPE_ANY = EFS_FIND_TYPE_SELF | EFS_FIND_TYPE_PARENT | EFS_FIND_TYPE_CHILD
} efs_find_type;

typedef struct efs_file_impl {
    efs_iterator it;
    efs_inode_iterator inode_it;
} efs_file_impl;

bool efs_inode_eq(efs_dir_entry* a, efs_dir_entry *b);
efs_dir_entry* efs_iterator_dir_entry(efs_iterator* it);
size_t efs_dir_entry_name_len(efs_dir_entry* entry);
bool efs_dir_entry_current(efs_dir_entry* entry);
bool efs_dir_entry_parent(efs_dir_entry* entry);
const char* efs_dir_entry_name(efs_dir_entry* entry, size_t* len);
bool efs_dir_entry_current_or_parent(efs_dir_entry* entry);
efs_inode_item* efs_dir_entry_inode_item(efs_dir_entry* entry);
loff_t efs_find_superblock(efs_context* ctx, efs_superblock* superblock, loff_t offset, loff_t end);
int efs_iterator_invalid(efs_iterator* it);
bool efs_iterator_is_valid(efs_context* ctx, efs_iterator* it);
int efs_iterator_sector(efs_context* ctx, efs_iterator* it, size_t sector);
int efs_iterator_root(efs_context* ctx, efs_iterator* it);
int efs_iterator_root_inode(efs_context* ctx, efs_iterator* it);
bool efs_iterator_is_root_inode(efs_context* ctx, efs_iterator* it);
uint32_t efs_dir_entry_inode_ref(efs_dir_entry* entry);
bool efs_find_check_type(efs_dir_entry* entry, efs_find_type type);
int efs_iterator_find_inode(efs_context* ctx, efs_iterator* it, uint32_t inode, efs_find_type type);
int efs_iterator_next_child_of(efs_context* ctx, efs_iterator* it, efs_dir_entry* parent, efs_find_type type);
int efs_iterator_next_sibling_of(efs_context* ctx, efs_iterator* it, efs_iterator* sibling, efs_find_type type);
bool efs_dir_entry_is_directory(efs_context* ctx, efs_dir_entry* entry);
int efs_iterator_next_node(efs_context* ctx, efs_iterator* it);
int efs_iterator_next_dirent(efs_context* ctx, efs_iterator* it);
int efs_read_inode(efs_context* ctx, efs_inode* inode, uint32_t ref);
size_t efs_inode_item_data_size(efs_dir_entry* entry);
size_t efs_dir_entry_size(const efs_dir_entry* entry);
int efs_read_dir_entry(efs_context* ctx, efs_iterator* it);
int efs_path_append(char* path, size_t len, efs_dir_entry* entry);
int efs_path_push(efs_context* ctx, efs_path* path, efs_iterator* it);
int efs_path_pop(efs_context* ctx, efs_path* path, efs_iterator* it);
bool efs_dir_entry_name_eq(efs_dir_entry* entry, const char* name, size_t len);
int efs_resolve_path(efs_context* ctx, const char* path, efs_iterator* it);
int efs_traverse(efs_context* ctx, efs_iterator* cur);
int efs_traverse_from_root(efs_context* ctx);
loff_t efs_find_group_descriptor(efs_context* ctx, efs_group_descriptor* desc);
loff_t efs_inode_size(efs_context* ctx, efs_dir_entry* entry);
bool efs_sector_valid(efs_context* ctx, uint32_t sector);
loff_t efs_inode_read(efs_context* ctx, efs_inode_iterator* it, void* buffer, loff_t offset, loff_t size);

#ifdef __cplusplus
}
#endif // __cplusplus
