#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2_utils.c"
#include "ext2_utils.h"
#include "ext2.h"

int total_fixes;
int fix_file(int, struct ext2_dir_entry *);
int skip(struct ext2_dir_entry*);
int loop_through_dir(int);

int skip(struct ext2_dir_entry* entry) {
    if (entry->file_type != EXT2_FT_DIR) {
        return 1;
    }
    if (strncmp(entry->name, ".", 1) == 0 && entry->name_len == 1) {
        return 1;
    }
    if (strncmp(entry->name, "..", 2) == 0 && entry->name_len == 2) {
        return 1;
    }
    return 0;
}

int loop_through_dir(int inode_id) {
    int fix_count = 0;
    struct ext2_inode* curr_dir = GET_INODE(inode_id);
    int curr_inode_id;
    struct ext2_dir_entry *curr_entry;
    for (int i = 0; i < 12 && curr_dir->i_block[i] != 0; i++) {
        curr_entry = (struct ext2_dir_entry*)(GET_BLOCK(curr_dir->i_block[i]));
        int j = 0;
        int rec_len = 0;
        while (j < EXT2_BLOCK_SIZE) {
            // update looping information
            curr_entry = (struct ext2_dir_entry *)((char *)curr_entry + rec_len);
            rec_len = curr_entry->rec_len;
            j += rec_len;
            // get the inode of the looped entry
            if (curr_entry->inode != 0) {
                curr_inode_id = curr_entry->inode;
                fix_count += fix_file(curr_inode_id, curr_entry);
                if (!skip(curr_entry)) {
                    fix_count += loop_through_dir(curr_inode_id);
                }
            }

        }
    }
    return fix_count;
}

int fix_file(int inode_id, struct ext2_dir_entry * entry) {
    int fix_count = 0;
    struct ext2_inode* curr_file = GET_INODE(inode_id);
    if (entry != NULL) {
        if ((curr_file->i_mode & EXT2_S_IFDIR) && entry->file_type != EXT2_FT_DIR) {
            printf("Fixed: Entry type vs inode mismatch: inode %d\n", inode_id);
            entry->file_type = EXT2_FT_DIR;
            fix_count++;
        } else if ((curr_file->i_mode & EXT2_S_IFLNK) && entry->file_type != EXT2_FT_SYMLINK) {
            printf("Fixed: Entry type vs inode mismatch: inode %d\n", inode_id);
            entry->file_type = EXT2_FT_SYMLINK;
            fix_count++;
        } else if ((curr_file->i_mode & EXT2_S_IFREG) && entry->file_type != EXT2_FT_REG_FILE) {
            printf("Fixed: Entry type vs inode mismatch: inode %d\n", inode_id);
            entry->file_type = EXT2_FT_REG_FILE;
            fix_count++;
        }
    }

    if(check_bitmap(inode_bitmap, inode_id) == 0) {
        change_bitmap(inode_bitmap, inode_id);
        printf("Fixed: inode %d not marked as in-use\n", inode_id);
        fix_count++;
        sb->s_free_inodes_count--;
        bg->bg_free_inodes_count--;
    }

    if (curr_file->i_dtime != 0) {
        if (curr_file->i_mode & EXT2_S_IFDIR) {
            bg->bg_used_dirs_count++;
        }
        printf("Fixed: valid inode marked for deletion: %d\n", inode_id);
        curr_file->i_dtime = 0;
        fix_count++;
    }

    for (int i = 0; i < 12 && curr_file->i_block[i] != 0; i++) {
        if (check_bitmap(block_bitmap, curr_file->i_block[i]) == 0) {
            change_bitmap(block_bitmap, curr_file->i_block[i]);
            fix_count++;
            sb->s_free_blocks_count--;
            bg->bg_free_blocks_count--;
            printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: %d\n", curr_file->i_block[i], inode_id);
        }
    }
    if (curr_file->i_block[12] != 0) {
        if (check_bitmap(block_bitmap, curr_file->i_block[12]) == 0) {
            change_bitmap(block_bitmap, curr_file->i_block[12]);
            fix_count++;
            sb->s_free_blocks_count--;
            bg->bg_free_blocks_count--;
            printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: %d\n", curr_file->i_block[12], inode_id);
        }
        int *block = (int *)GET_BLOCK(curr_file->i_block[12]);
        for (int k = 0; k < 128 && block[k] != 0; k++) {
            if (check_bitmap(block_bitmap, block[k]) == 0) {
                change_bitmap(block_bitmap, block[k]);
                fix_count++;
                sb->s_free_blocks_count--;
                bg->bg_free_blocks_count--;
                printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: %d\n", block[k], inode_id);
            }
        }
    }
    return fix_count;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    //initialize the disk
    init_disk(argv[1]);
    total_fixes = 0;

    // fix bitmap and free block/inode counts
    int num_free = 0;
    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 8; j++) {
            if(!(*(inode_bitmap + i) >> j & 1)) {
                num_free++;
            } 
        }
    }

    if (num_free != sb->s_free_inodes_count) {
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", abs(num_free - sb->s_free_inodes_count));
        sb->s_free_inodes_count = num_free;
        total_fixes++;
    }

    if (num_free != bg->bg_free_inodes_count) {
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", abs(num_free - bg->bg_free_inodes_count));
        bg->bg_free_inodes_count = num_free;
        total_fixes++;
    }

    // check block bitmap
    num_free = 0;
    for(int i = 0; i < 16; i++) {
        for(int j = 0; j < 8; j++) {
            if(!(*(block_bitmap + i) >> j & 1)) {
                num_free++;
            } 
        }
    }

    if (num_free != sb->s_free_blocks_count) {
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", abs(num_free - sb->s_free_blocks_count));
        sb->s_free_blocks_count = num_free;
        total_fixes++;
    }

    if (num_free != bg->bg_free_blocks_count) {
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", abs(num_free - bg->bg_free_blocks_count));
        bg->bg_free_blocks_count = num_free;
        total_fixes++;
    }

    total_fixes += fix_file(root_inode, NULL);
    total_fixes += loop_through_dir(root_inode);
    if (total_fixes) {
        printf("%d file system inconsistencies repaired!\n", total_fixes);
    } else {
        printf("No file system inconsistencies detected!\n");
    }
    return 0;
}