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


int main(int argc, char **argv) {
    init_disk(argv[1]);
    char* restored_file_name;
    int parent_inode_id = find_parent_by_abs_path(&restored_file_name, argv[2]);
    if (parent_inode_id == -1) {
        perror("You should not recover root directory\n");
        return ENOENT;
    } else if (parent_inode_id == -2) {
        perror("Cannot find the given file\n");
        return ENOENT;
    }
    int name_len = strlen(restored_file_name);

    int entry_found = 0;
    struct ext2_dir_entry* deleted_entry;
    struct ext2_dir_entry* prev_entry;
    int offset_in_prev_entry;
    // search all blocks for the current inode
    struct ext2_inode* curr_inode = (struct ext2_inode*)(GET_INODE(parent_inode_id));
    for(int i = 0; i < 12 && curr_inode->i_block[i] != 0 && entry_found == 0; i++) {
        struct ext2_dir_entry *curr_dir = (struct ext2_dir_entry *)(GET_BLOCK(curr_inode->i_block[i]));
        int j = 0;
        int rec_len = 0;
        int entry_size = 0;
        while (j < EXT2_BLOCK_SIZE) {
            curr_dir = (struct ext2_dir_entry *)((char *)curr_dir + rec_len);
            rec_len = curr_dir->rec_len;
            j += rec_len;
            entry_size = get_entry_size(curr_dir->name_len);
            int k = entry_size;
            struct ext2_dir_entry * deleted_dir = curr_dir;
            while (k < rec_len) {
                deleted_dir = (struct ext2_dir_entry *)((char *)deleted_dir + entry_size);
                entry_size = get_entry_size(deleted_dir->name_len);
                if (deleted_dir->name_len == name_len) {
                    if (strncmp(deleted_dir->name, restored_file_name, name_len) == 0 && deleted_dir->file_type != EXT2_FT_DIR) {
                        prev_entry = curr_dir;
                        deleted_entry = deleted_dir;
                        offset_in_prev_entry = k;
                        entry_found = 1;
                        break;
                    }
                }
                k += entry_size;
            }
        }
    }

    if (entry_found) {
        // if we can find a deleted inode with the given file name
        // then try to restore it
        int deleted_inode_id = deleted_entry->inode;
        // check if some other files has already taken the deleted inode
        if (check_bitmap(inode_bitmap, deleted_inode_id)) {
            perror("The inode of the file has already been taken by other files, the file is not recoverable");
            return ENOENT;
        }
        struct ext2_inode* deleted_inode= GET_INODE(deleted_inode_id);
        // get all blocks we need to restore the file
        int i;
        for (i = 0; i < 12 && deleted_inode->i_block[i] != 0; i++) {
            if (check_bitmap(block_bitmap, deleted_inode->i_block[i])) {
                perror("Ones of the blocks of the file has already been taken by other files, the file is not recoverable");
                return ENOENT;
            }
        }
        // check whether the recovering file has indirect block pointer
        if (deleted_inode->i_block[12] != 0) {
            if(check_bitmap(block_bitmap, deleted_inode->i_block[12])) {
                perror("Ones of the blocks of the file has already been taken by other files, the file is not recoverable");
                return ENOENT;
            }
            int * indirect_block = (int *)GET_BLOCK(deleted_inode->i_block[12]);
            for (i = 0; i < 128 && indirect_block[i] != 0; i++) {
                if (check_bitmap(block_bitmap, indirect_block[i])) {
                    perror("Ones of the blocks of the file has already been taken by other files, the file is not recoverable");
                    return ENOENT;
                }
            }
        }
        // if everything we need is available for the recovering file
        // recover the entry in the original parent file
        deleted_entry->rec_len = prev_entry->rec_len - offset_in_prev_entry;
        prev_entry->rec_len = offset_in_prev_entry;

        change_bitmap(inode_bitmap, deleted_inode_id);
        sb->s_free_inodes_count--;
        bg->bg_free_inodes_count--;
        ((struct ext2_inode *)GET_INODE(deleted_inode_id))->i_dtime = 0;
        ((struct ext2_inode *)GET_INODE(deleted_inode_id))->i_links_count++;

        // restore all blocks of the file
        for (i = 0; i < 12 && deleted_inode->i_block[i] != 0; i++) {
            change_bitmap(block_bitmap, deleted_inode->i_block[i]);
            sb->s_free_blocks_count--;
            bg->bg_free_blocks_count--;
        }
        if (deleted_inode->i_block[12] != 0) {
            change_bitmap(block_bitmap, deleted_inode->i_block[12]);
            sb->s_free_blocks_count--;
            bg->bg_free_blocks_count--;
            int * indirect_block = (int *)GET_BLOCK(deleted_inode->i_block[12]);
            for (i = 0; i < 128 && indirect_block[i] != 0; i++) {
                change_bitmap(block_bitmap, indirect_block[i]);
                sb->s_free_blocks_count--;
                bg->bg_free_blocks_count--;
            }
        }

    } else {
        // if we cannot find the deleted file
        perror("Cannot find the file want to recover");
        return ENOENT;
    }
    return 0;
}