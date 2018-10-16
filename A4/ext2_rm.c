#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h> 
#include "ext2_utils.c"
#include "ext2_utils.h"
#include "ext2.h"

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }

    //initialize the disk
    init_disk(argv[1]);

    char *abs_path = argv[2];
    char *deleted_file_name;
    // find the parent of the given dir first
    int parent_inode_id = find_parent_by_abs_path(&deleted_file_name, abs_path);
    if (parent_inode_id == -2) {
        perror("no entry\n");
        return ENOENT;
    } else if(parent_inode_id == -1) {
        perror("Cannot delete root directory");
    }
    // find the inode for the file to be deleted
    int target_inode_id = search_for_curr_dir(parent_inode_id, deleted_file_name, NOTDIR);
    if (target_inode_id == -1) {
        perror("no entry\n");
        return ENOENT;
    }
    struct ext2_inode *target_inode = GET_INODE(target_inode_id);
    // search all the blocks that parent inode uses
    struct ext2_inode *parent_inode = GET_INODE(parent_inode_id);
    for (int i = 0; i < 12 && parent_inode->i_block[i]; i++) {
        int rec_len = 0;
        while (rec_len < EXT2_BLOCK_SIZE) {
            // save the entry before adding the rec_len
            // so that when we find the file to be deleted, we can change the previous entry
            struct ext2_dir_entry *before = (struct ext2_dir_entry *)((char *)GET_BLOCK(parent_inode->i_block[i]) + rec_len);
            rec_len += before->rec_len;
            struct ext2_dir_entry *next = (struct ext2_dir_entry *)((char *)GET_BLOCK(parent_inode->i_block[i]) + rec_len);
            if (next->file_type == EXT2_FT_REG_FILE && strncmp(next->name, deleted_file_name, next->name_len) == 0) {
                before->rec_len = before->rec_len + next->rec_len;
                // reduce links count
                target_inode->i_links_count--;
                if (GET_INODE(next->inode)->i_links_count == 0) {
                    // set dtime and change bitmap
                    target_inode->i_dtime = time(NULL);
                    change_bitmap(inode_bitmap, next->inode);
                    sb->s_free_inodes_count++;
                    bg->bg_free_inodes_count++;
                    for (int k = 0; k < 12 && target_inode->i_block[k]; k++) {
                        change_bitmap(block_bitmap, target_inode->i_block[k]);
                        sb->s_free_blocks_count++;
                        bg->bg_free_blocks_count++;
                    }
                    // indirect link
                    if (target_inode->i_block[12] != 0) {
                        change_bitmap(block_bitmap, target_inode->i_block[12]);
                        sb->s_free_blocks_count++;
                        bg->bg_free_blocks_count++;
                        int * block = (int *)GET_BLOCK(target_inode->i_block[12]);
                        for (int k = 0; k < 128 && block[k] != 0; k++) {
                            change_bitmap(block_bitmap, block[k]);
                            sb->s_free_blocks_count++;
                            bg->bg_free_blocks_count++;
                        }
                    }
                }
            }
        }
    }
    return 0;
}