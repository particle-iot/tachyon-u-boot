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

typedef enum efs_error {
    EFS_ERROR_NONE = 0,
    EFS_ERROR_GENERIC = -1,
    EFS_ERROR_EOF = -2,
} efs_error;

typedef enum efs_loglevel {
    EFS_LOG_NONE = 0,
    EFS_LOG_INFO = 1,
    EFS_LOG_WARN = 2,
    EFS_LOG_ERROR = 3,
    EFS_LOG_DEBUG = 4
} efs_loglevel;

typedef struct efs_ops {
    void* ctx;
    loff_t (*read)(void* buf, loff_t offset, loff_t size, void *ctx);
    int (*logger)(efs_loglevel level, void* ctx, const char* fmt, ...);
} efs_ops;

typedef struct efs_context {
    efs_ops ops;

    loff_t superblock_offset;

    size_t sector_size;
    size_t sector_count;
    size_t sectors_per_block;

    uint32_t database_root;

    uint32_t inode_root;
} efs_context;

typedef struct efs_file {

} efs_file;

typedef struct efs_dir {

} efs_dir;

typedef enum efs_type {
    EFS_TYPE_UNKNOWN = 0,
    EFS_TYPE_FILE = 1,
    EFS_TYPE_DIR = 2
} efs_type;

#define EFS_FILE_NAME_MAX_LEN (255)
#define EFS_MAX_PATH (4096)

#define EFS_PATH_CURRENT_DIR_NAME "."
#define EFS_PATH_PARENT_DIR_NAME ".."
#define EFS_PATH_SEPARATOR '/'

typedef struct efs_dirent {
    uint8_t type; // efs_type
    loff_t size;

    char name[EFS_FILE_NAME_MAX_LEN + 1];
} efs_dirent;

int efs_mount(efs_context* ctx, efs_ops ops);
int efs_unmount(efs_context* ctx);

int efs_open(efs_context* ctx, efs_file** file, const char* path, int flags);
int efs_stat(efs_context* ctx, const char* path, efs_dirent* ent);
int efs_read(efs_context* ctx, efs_file* file, void* buffer, loff_t offset, loff_t size);
int efs_close(efs_context* ctx, efs_file* file);

int efs_opendir(efs_context* ctx, efs_dir** dir, const char* path);
int efs_readdir(efs_context* ctx, efs_dir* dir, efs_dirent* ent);
int efs_closedir(efs_context* ctx, efs_dir* dir);

#ifdef __cplusplus
}
#endif // __cplusplus
