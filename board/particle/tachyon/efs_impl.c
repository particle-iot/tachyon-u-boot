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

bool efs_inode_eq(efs_dir_entry* a, efs_dir_entry *b) {
    return efs_dir_entry_inode_ref(a) == efs_dir_entry_inode_ref(b);
}

efs_dir_entry* efs_iterator_dir_entry(efs_iterator* it) {
    return (efs_dir_entry*)it->dirent;
}

size_t efs_dir_entry_name_len(efs_dir_entry* entry) {
    if (entry->size == 0) {
        return 0;
    }
    return entry->size - sizeof(entry->type) - sizeof(entry->parent_inode);
}

bool efs_dir_entry_current(efs_dir_entry* entry) {
    if (entry->size == 0) {
        return false;
    }
    uint32_t inode = efs_dir_entry_inode_ref(entry);
    if (inode != EFS_INVALID_INODE && entry->parent_inode == inode) {
        if (efs_dir_entry_name_len(entry) == 0) {
            return true;
        }
    }
    return false;
}

bool efs_dir_entry_parent(efs_dir_entry* entry) {
    if (entry->size == 0) {
        return false;
    }
    uint32_t inode = efs_dir_entry_inode_ref(entry);
    if (inode != EFS_INVALID_INODE) {
        if (efs_dir_entry_name_len(entry) == 1 && entry->name[0] == '\0') {
            return true;
        }
    }
    return false;
}


const char* efs_dir_entry_name(efs_dir_entry* entry, size_t* len) {
    size_t name_len = efs_dir_entry_name_len(entry);
    const char* name = entry->name;
    // XXX: should probably check the inode == parent inode, but works fine as-is
    if (efs_dir_entry_current(entry)) {
        name = EFS_PATH_CURRENT_DIR_NAME;
        *len = sizeof(EFS_PATH_CURRENT_DIR_NAME) - 1;
    } else if (efs_dir_entry_parent(entry)) {
        name = EFS_PATH_PARENT_DIR_NAME;
        *len = sizeof(EFS_PATH_PARENT_DIR_NAME) - 1;
    } else {
        *len = name_len;
    }

    return name;
}

bool efs_dir_entry_current_or_parent(efs_dir_entry* entry) {
    return efs_dir_entry_current(entry) || efs_dir_entry_parent(entry);
}

efs_inode_item* efs_dir_entry_inode_item(efs_dir_entry* entry) {
    if (entry->size == 0) {
        return NULL;
    }
    efs_inode_item* inode = (efs_inode_item*)(entry->name + efs_dir_entry_name_len(entry));
    return inode;
}

loff_t efs_find_superblock(efs_context* ctx, efs_superblock* superblock, loff_t offset, loff_t end) {
    for (; (offset + sizeof(efs_superblock)) <= end; offset++) {
        EFS_CHECK(ctx->ops.read(superblock, offset, sizeof(efs_superblock), ctx->ops.ctx));
        
        if (superblock->header != EFS_SUPER_BLOCK_HEADER) {
            continue;
        }
        if (strncmp((const char*)superblock->magic, EFS_SUPER_BLOCK_MAGIC, sizeof(superblock->magic))) {
            continue;
        }
        EFS_LOG(INFO, ctx, "Found superblock at offset %lld", offset);
        return offset;
    }
    EFS_LOG(ERROR, ctx, "Couldn't find superblock");
    return EFS_ERROR_GENERIC;
}

inline loff_t efs_raw_sector_to_offset(efs_context* ctx, size_t sector) {
    return (loff_t)ctx->sector_size * sector;
}

inline loff_t efs_sector_to_offset(efs_context* ctx, size_t sector) {
    return ctx->superblock_offset + efs_raw_sector_to_offset(ctx, sector + 1);
}

inline size_t efs_offset_to_sector(efs_context* ctx, loff_t offset) {
    offset -= ctx->superblock_offset;
    offset -= ctx->sector_size;
    return offset / ctx->sector_size;
}

inline loff_t efs_block_to_offset(efs_context* ctx, size_t block) {
    return ctx->superblock_offset + ctx->sector_size * ctx->sectors_per_block * (block + 1);
}

int efs_iterator_invalid(efs_iterator* it) {
    memset(it, 0, sizeof(efs_iterator));
    it->sector = EFS_INVALID_SECTOR;
    return 0;
}

bool efs_iterator_is_valid(efs_context* ctx, efs_iterator* it) {
    if (it->sector == EFS_INVALID_SECTOR) {
        return false;
    }

    if (it->sector > ctx->sector_count) {
        return false;
    }

    return true;
}

int efs_iterator_sector(efs_context* ctx, efs_iterator* it, size_t sector) {
    EFS_CHECK(ctx->ops.read(&it->node, efs_sector_to_offset(ctx, sector), sizeof(efs_node), ctx->ops.ctx));
    it->sector = sector;

    it->pos = 0;
    memset(it->dirent, 0, sizeof(it->dirent));
    return 0;
}

int efs_iterator_root(efs_context* ctx, efs_iterator* it) {
    return efs_iterator_sector(ctx, it, efs_offset_to_sector(ctx, efs_block_to_offset(ctx, ctx->database_root)));
}

int efs_iterator_root_inode(efs_context* ctx, efs_iterator* it) {
    return efs_iterator_find_inode(ctx, it, ctx->inode_root, EFS_FIND_TYPE_SELF);
}

bool efs_iterator_is_root_inode(efs_context* ctx, efs_iterator* it) {
    efs_dir_entry* entry = efs_iterator_dir_entry(it);
    return efs_dir_entry_inode_ref(entry) == ctx->inode_root && entry->parent_inode == ctx->inode_root;
}

uint32_t efs_dir_entry_inode_ref(efs_dir_entry* entry) {
    if (entry && efs_dir_entry_inode_item(entry)->type == EFS_INODE_ITEM_REF) {
        efs_inode_item_ref* ref = (efs_inode_item_ref*)(efs_dir_entry_inode_item(entry)->data);
        return ref->ref;
    }
    return EFS_INVALID_INODE;
}

bool efs_find_check_type(efs_dir_entry* entry, efs_find_type type) {
    if ((type & EFS_FIND_TYPE_ANY) == EFS_FIND_TYPE_ANY) {
        return true;
    }
    if (type & EFS_FIND_TYPE_SELF) {
        if (efs_dir_entry_current(entry)) {
            // Found
            return true;
        }
    }
    if (type & EFS_FIND_TYPE_PARENT) {
        if (efs_dir_entry_parent(entry)) {
            // Found
            return true;
        }
    }
    if (type & EFS_FIND_TYPE_CHILD) {
        if (!efs_dir_entry_current_or_parent(entry)) {
            return true;
        }
    }
    return false;
}

int efs_iterator_find_inode(efs_context* ctx, efs_iterator* it, uint32_t inode, efs_find_type type) {
    if (!efs_iterator_is_valid(ctx, it)) {
        EFS_CHECK(efs_iterator_root(ctx, it));
    }
    while (true) {
        int r = efs_iterator_next_dirent(ctx, it);
        if (r == EFS_ERROR_EOF) {
            EFS_CHECK(efs_iterator_next_node(ctx, it));
            continue;
        }
        EFS_CHECK(r);
        efs_dir_entry* entry = efs_iterator_dir_entry(it);
        if (efs_dir_entry_inode_ref(entry) == inode) {
            if (efs_find_check_type(entry, type)) {
                return 0;
            }
        }
    }
    return EFS_ERROR_EOF;
}

int efs_iterator_next_child_of(efs_context* ctx, efs_iterator* it, efs_dir_entry* parent, efs_find_type type) {
    while (true) {
        int r = efs_iterator_next_dirent(ctx, it);
        if (r == EFS_ERROR_EOF) {
            EFS_CHECK(efs_iterator_next_node(ctx, it));
            continue;
        }
        EFS_CHECK(r);
        efs_dir_entry* entry = efs_iterator_dir_entry(it);
        if (entry->parent_inode == efs_dir_entry_inode_ref(parent)) {
            if (efs_find_check_type(entry, type)) {
                return 0;
            }
        }
    }
    return EFS_ERROR_EOF;
}

int efs_iterator_next_sibling_of(efs_context* ctx, efs_iterator* it, efs_iterator* sibling, efs_find_type type) {
    bool seen_sibling_sector = false;
    while (true) {
        if (it->sector == sibling->sector) {
            seen_sibling_sector = true;
        }
        int r = efs_iterator_next_dirent(ctx, it);
        if (r == EFS_ERROR_EOF) {
            EFS_CHECK(efs_iterator_next_node(ctx, it));
            continue;
        }
        EFS_CHECK(r);
        efs_dir_entry* entry = efs_iterator_dir_entry(it);
        if (entry->parent_inode != efs_iterator_dir_entry(sibling)->parent_inode) {
            break;
        }

        if ((it->sector == sibling->sector && it->pos > sibling->pos) || seen_sibling_sector) {
            if (efs_find_check_type(entry, type)) {
                return 0;
            }
        }
    }
    return EFS_ERROR_EOF;
}


bool efs_dir_entry_is_directory(efs_context* ctx, efs_dir_entry* entry) {
    if (efs_dir_entry_inode_item(entry)->type != EFS_INODE_ITEM_REF) {
        return false;
    }
    efs_inode inode = {};
    efs_inode_item_ref* ref = (efs_inode_item_ref*)(efs_dir_entry_inode_item(entry)->data);
    if (efs_read_inode(ctx, &inode, ref->ref) >= 0) {
        return (inode.mode & S_IFDIR);
    } else {
        EFS_LOG(ERROR, ctx, "error reading inode");
    }
    return false;
}

int efs_iterator_next_node(efs_context* ctx, efs_iterator* it) {
    size_t next = it->node.next;
    if (next == EFS_INVALID_SECTOR) {
        return EFS_ERROR_EOF;
    }
    return efs_iterator_sector(ctx, it, next);
}

int efs_iterator_next_dirent(efs_context* ctx, efs_iterator* it) {
    if (it->pos == 0) {
        it->pos = sizeof(efs_node);
    } else {
        it->pos += efs_dir_entry_size(efs_iterator_dir_entry(it));
    }
    EFS_CHECK(efs_read_dir_entry(ctx, it));
    return 0;
}

int efs_read_inode(efs_context* ctx, efs_inode* inode, uint32_t ref) {
    size_t sector = ref >> EFS_INODE_INDEX_BITS;
    size_t index = ref & ((1 << EFS_INODE_INDEX_BITS) - 1);
    return EFS_CHECK(ctx->ops.read(inode, efs_sector_to_offset(ctx, sector) + sizeof(efs_inode) * index, sizeof(efs_inode), ctx->ops.ctx));
}

size_t efs_inode_item_data_size(efs_dir_entry* entry) {
    if (entry->inode_item_size == 0) {
        return 0;
    }
    efs_inode_item* item = efs_dir_entry_inode_item(entry);
    size_t size = entry->inode_item_size - sizeof(item->type);
    switch (item->type) {
        case EFS_INODE_ITEM_REF: {
            return size;
        }
        case EFS_INODE_ITEM_INLINE: {
            return size - sizeof(efs_inode_item_inline);
        }
        case EFS_INODE_ITEM_INLINE_EXT: {
            return size - sizeof(efs_inode_item_inline_ext);
        }
    }
    return 0;
}

size_t efs_dir_entry_size(const efs_dir_entry* entry) {
    return sizeof(entry->size) + sizeof(entry->inode_item_size) + entry->size + entry->inode_item_size;
}

int efs_read_dir_entry(efs_context* ctx, efs_iterator* it) {
    loff_t pos = it->pos;
    loff_t used = sizeof(efs_node) + it->node.used;
    if (used - pos < sizeof(efs_dir_entry)) {
        return EFS_ERROR_EOF;
    }

    efs_dir_entry header = {};
    EFS_CHECK(ctx->ops.read(&header, efs_sector_to_offset(ctx, it->sector) + pos, sizeof(efs_dir_entry), ctx->ops.ctx));
    if (header.type != EFS_DIR_ENTRY_TYPE) {
        // Not a valid entry
        return EFS_ERROR_GENERIC;
    }
    size_t size = efs_dir_entry_size(&header);
    EFS_CHECK(ctx->ops.read(it->dirent, efs_sector_to_offset(ctx, it->sector) + pos, size, ctx->ops.ctx));
    return size;
}

int efs_path_append(char* path, size_t len, efs_dir_entry* entry) {
    size_t name_len = 0;
    const char* name = efs_dir_entry_name(entry, &name_len);
    if ((name_len + 1) > len) {
        return EFS_ERROR_GENERIC;
    }
    strncpy(path, name, name_len);
    path[name_len] = '\0';
    return name_len;
}

int efs_path_push(efs_context* ctx, efs_path* path, efs_iterator* it) {
    if (path->path_pos == 0) {
        size_t name_len = EFS_CHECK(efs_path_append(path->path + path->path_pos, EFS_MAX_PATH - path->path_pos, efs_iterator_dir_entry(it)));
        path->path_pos += name_len;
        if (efs_iterator_is_root_inode(ctx, it)) {
            path->path[path->path_pos - 1] = '/';
        }
    } else {
        if (path->path[path->path_pos - 1] != '/') {
            path->path[path->path_pos++] = '/';
        }
        size_t name_len = EFS_CHECK(efs_path_append(path->path + path->path_pos, EFS_MAX_PATH - path->path_pos, efs_iterator_dir_entry(it)));
        path->path_pos += name_len;
    }
    EFS_LOG(INFO, ctx, "%s", path->path);
    return path->path_pos;
}

int efs_path_pop(efs_context* ctx, efs_path* path, efs_iterator* it) {
    path->path_pos -= efs_dir_entry_name_len(efs_iterator_dir_entry(it));
    if (path->path_pos > 1 && path->path[path->path_pos - 1] == '/') {
        path->path_pos--;
        path->path[path->path_pos] = '\0';
    }
    return path->path_pos;
}

bool efs_dir_entry_name_eq(efs_dir_entry* entry, const char* name, size_t len) {
    size_t name_len = 0;
    const char* dname = efs_dir_entry_name(entry, &name_len);
    if (name_len == len && !strncmp(name, dname, len)) {
        return true;
    }
    return false;
}

int efs_resolve_path(efs_context* ctx, const char* path, efs_iterator* it) {
    const char* start = path;
    while (*start != '\0' && *start == EFS_PATH_SEPARATOR) {
        start++;
    }
    efs_iterator root = {};
    efs_iterator_invalid(&root);
    efs_iterator_root_inode(ctx, &root);
    for (const char* p = start; *(p - 1) != '\0'; p++) {
        if (*p == EFS_PATH_SEPARATOR || *p == '\0') {
            size_t len = p - start;
            const char* path_piece = start;
            start = p + 1;
            EFS_CHECK(efs_iterator_root(ctx, it));
            while (EFS_CHECK(efs_iterator_next_child_of(ctx, it, efs_iterator_dir_entry(&root), EFS_FIND_TYPE_CHILD)) >= 0) {
                if (efs_dir_entry_name_eq(efs_iterator_dir_entry(it), path_piece, len)) {
                    root = *it;
                    break;
                }
            }
        }
    }
    return 0;
}

int efs_traverse(efs_context* ctx, efs_iterator* cur) {
    EFS_CHECK_TRUE(efs_iterator_is_valid(ctx, cur), EFS_ERROR_GENERIC);

    efs_iterator it = {};
    efs_iterator root = *cur;
    efs_iterator parent = *cur;
    efs_path path = {};

    efs_iterator_invalid(&parent);

    efs_path_push(ctx, &path, cur);

    bool complete = false;

    while (!complete) {
        // Current is directory, DFS - go down the first child
        if (efs_dir_entry_is_directory(ctx, efs_iterator_dir_entry(cur))) {

            EFS_CHECK(efs_iterator_root(ctx, &it));
            if (efs_iterator_next_child_of(ctx, &it, efs_iterator_dir_entry(cur), EFS_FIND_TYPE_CHILD /* skip . and ..*/) >= 0) {
                efs_path_push(ctx, &path, &it);

                parent = *cur;
                *cur = it;
                continue;
            }
        }
        
        if (efs_iterator_is_valid(ctx, &parent)) {
            // Current is not a directory, but parent exists - find next sibling == find next child of parent
            it = *cur;

            efs_path_pop(ctx, &path, cur);

            if (efs_iterator_next_child_of(ctx, &it, efs_iterator_dir_entry(&parent), EFS_FIND_TYPE_CHILD) >= 0) {
                // Found sibling (next child of parent)
                efs_path_push(ctx, &path, &it);

                *cur = it;
                continue;
            }

            // No more siblings at parent, (go up two levels, find next sibling, fail - go up another level)
            // It might be more optimal to go the opposite direction, but who knows how things are layed out on disk
            while (!complete) {
                *cur = parent; // ..

                if (efs_inode_eq(efs_iterator_dir_entry(cur), efs_iterator_dir_entry(&root))) {
                    complete = true;
                    break;
                }

                efs_path_pop(ctx, &path, cur);

                EFS_CHECK(efs_iterator_root(ctx, &it));
                efs_find_type type = EFS_FIND_TYPE_CHILD;
                if (efs_iterator_dir_entry(cur)->parent_inode == ctx->inode_root) {
                    type = EFS_FIND_TYPE_SELF;
                }
                if (efs_iterator_find_inode(ctx, &it, efs_iterator_dir_entry(cur)->parent_inode, type) >= 0) {
                    parent = it;
                    it = *cur;

                    if (efs_iterator_next_sibling_of(ctx, &it, cur, EFS_FIND_TYPE_CHILD) >= 0) {
                        *cur = it;
                        // If we find sibling at this level - go with it
                        efs_path_push(ctx, &path, cur);
                        break;
                    }
                }
            }
        }
    }
    
    return 0;
}


int efs_traverse_from_root(efs_context* ctx) {
    efs_iterator it;
    efs_iterator_invalid(&it);
    EFS_CHECK(efs_iterator_root_inode(ctx, &it));
    return efs_traverse(ctx, &it);
}

loff_t efs_find_group_descriptor(efs_context* ctx, efs_group_descriptor* desc) {
    for (size_t sector = efs_block_to_offset(ctx, 0) / ctx->sector_size; sector < ctx->sector_count; sector++) {
        loff_t pos = efs_raw_sector_to_offset(ctx, sector);
        EFS_CHECK(ctx->ops.read(desc, pos, sizeof(efs_group_descriptor), ctx->ops.ctx));
        if (desc->header != EFS_GROUP_DESCRIPTOR_HEADER) {
            continue;
        }
        EFS_LOG(INFO, ctx, "Found group descriptor at offset %lld (sector %lu)", pos, sector);
        return pos;
    }
    EFS_LOG(ERROR, ctx, "Couldn't find group descriptor");
    return EFS_ERROR_GENERIC;
}

loff_t efs_inode_size(efs_context* ctx, efs_dir_entry* entry) {
    efs_inode_item* item = efs_dir_entry_inode_item(entry);

    if (item->type == EFS_INODE_ITEM_REF) {
        efs_inode inode = {};
        EFS_CHECK(efs_read_inode(ctx, &inode, efs_dir_entry_inode_ref(entry)));
        return inode.size;
    } else if (item->type == EFS_INODE_ITEM_INLINE) {
        return efs_inode_item_data_size(entry);
    } else if (item->type == EFS_INODE_ITEM_INLINE_EXT) {
        return efs_inode_item_data_size(entry);
    }
    return 0;
}

bool efs_sector_valid(efs_context* ctx, uint32_t sector) {
    if (sector == EFS_INVALID_SECTOR || sector > ctx->sector_count) {
        return false;
    }
    return true;
}

loff_t efs_inode_read(efs_context* ctx, efs_inode_iterator* it, void* buffer, loff_t offset, loff_t size) {
    loff_t pos = 0;
    offset = min(offset, (loff_t)it->inode.size);
    size = min(size, (loff_t)it->inode.size - offset);

    size_t sectors_to_skip = offset % ctx->sector_size;
    loff_t bytes_to_skip = offset - sectors_to_skip * ctx->sector_size;

    for (size_t i = sectors_to_skip; i < sizeof(it->inode.sectors) / sizeof(it->inode.sectors[0]) && pos < size; i++) {
        if (!efs_sector_valid(ctx, it->inode.sectors[i])) {
            EFS_LOG(ERROR, ctx, "Invalid sector %08x", it->inode.sectors[i]);
            return EFS_ERROR_EOF;
        }
        pos += EFS_CHECK(ctx->ops.read((char*)buffer + pos, efs_sector_to_offset(ctx, it->inode.sectors[i]) + bytes_to_skip, min((loff_t)ctx->sector_size - bytes_to_skip, size - pos), ctx->ops.ctx));
        bytes_to_skip = 0;
    }

    size_t indirect_idx = 0;
    size_t sectors_per_indirect = ctx->sector_size / sizeof(uint32_t);
    if (sectors_to_skip > 0) {
        indirect_idx = sectors_to_skip / sectors_per_indirect;
        sectors_to_skip -= indirect_idx * sectors_per_indirect;
    }

    for (size_t i = indirect_idx; i < sizeof(it->inode.sectors_indirect) / sizeof(it->inode.sectors_indirect[0]) && pos < size; i++) {
        for (size_t sector_idx = sectors_to_skip; sector_idx < sectors_per_indirect && pos < size; sector_idx++) {
            uint32_t sector = it->inode.sectors_indirect[i];
            if (!efs_sector_valid(ctx, sector)) {
                EFS_LOG(ERROR, ctx, "Invalid sector %08x", sector);
                return EFS_ERROR_EOF;
            }
            uint32_t ref_sector = EFS_INVALID_SECTOR;
            EFS_CHECK(ctx->ops.read(&ref_sector, efs_sector_to_offset(ctx, sector) + sector_idx * sizeof(uint32_t), sizeof(uint32_t), ctx->ops.ctx));
            if (!efs_sector_valid(ctx, ref_sector)) {
                EFS_LOG(ERROR, ctx, "Invalid sector %08x", ref_sector);
                return EFS_ERROR_EOF;
            }
            pos += EFS_CHECK(ctx->ops.read((char*)buffer + pos, efs_sector_to_offset(ctx, ref_sector) + bytes_to_skip, min((loff_t)ctx->sector_size - bytes_to_skip, size - pos), ctx->ops.ctx));
            bytes_to_skip = 0;
        }
        sectors_to_skip = 0;
    }
    return pos;
}
