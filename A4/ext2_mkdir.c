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


extern unsigned char *disk;

int main(int argc, char **argv) {
    
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }

    //initialize the disk
    init_disk(argv[1]);

    char *abs_path = argv[2];
    char *created_dir_name;
    // find the parent of the given dir first
    int parent_inode_id = find_parent_by_abs_path(&created_dir_name, abs_path);
    if (strlen(created_dir_name) > EXT2_NAME_LEN) {
        perror("the name of the directory is too long");
        return ENOENT;
    }
    if (parent_inode_id < 0) {
        // if we are not able to find the parent dir, return the error
        perror("no entry\n");
        return ENOENT;
    }
    if (search_for_curr_dir(parent_inode_id, created_dir_name, DIR) != -1) {
        // if we have already had the directory we want to creat, return the error
        perror("Dir exists\n");
        return EEXIST;
    }

    // find unused inode space in bitmap
    int new_inode_id = find_free_in_inode_bitmap();
    // find unused block
    int new_block_id = find_free_in_block_bitmap();
    if(new_inode_id == -1 || new_block_id == -1) {
        perror("no free position\n");
        exit(1);
    }

    new_inode_id = get_free_inode();
    new_block_id = get_free_block();
    
    // initialize new inode
    struct ext2_inode *new_inode = GET_INODE(new_inode_id);
    new_inode->i_mode = EXT2_S_IFDIR;
    for(int j = 1; j < 15; j++) {
        new_inode->i_block[j] = 0;
    }
    new_inode->i_block[0]  = new_block_id;
    new_inode->i_blocks = 2;
    new_inode->i_size = EXT2_BLOCK_SIZE;
    new_inode->i_dtime = 0;
    // parent link + self link
    new_inode->i_links_count = 2;

    // write info into dir entry
    struct ext2_dir_entry *new_dir = (struct ext2_dir_entry *)(GET_BLOCK(new_block_id));
    new_dir->inode = new_inode_id;
    strcpy(new_dir->name, ".");
    new_dir->name_len = 1;
    new_dir->rec_len = get_entry_size(1);
    new_dir->file_type = EXT2_FT_DIR;
    
    new_dir = (struct ext2_dir_entry *)((char *)new_dir + new_dir->rec_len);
    new_dir->inode = parent_inode_id;
    strcpy(new_dir->name, "..");
    new_dir->name_len = 2;
    new_dir->rec_len = EXT2_BLOCK_SIZE - get_entry_size(1);
    new_dir->file_type = EXT2_FT_DIR;

    // write info into parent inode
    int result = write_new_entry_to_dir(parent_inode_id, new_inode_id, created_dir_name, EXT2_FT_DIR);
    if (result == 1) {
        bg->bg_used_dirs_count++;
        (GET_INODE(parent_inode_id)->i_links_count)++;
        return 0;
    }
    return result;
}

