#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include "ext2_utils.c"
#include "ext2_utils.h"
#include "ext2.h"



int main(int argc, char **argv) {
    if(argc != 4) {
        perror("Usage: ext2_cp <image file name> <SOURCE> <DEST>\n");
        exit(1);
    }
    init_disk(argv[1]);

    char * source_path = argv[2];
    int source = open(source_path, O_RDONLY);
    if (source < 0) {
        perror("No entry\n");
        return ENOENT;
    }
    char* dest_path = argv[3];
    char dest_path1[strlen(dest_path) + 1];
    strcpy(dest_path1, dest_path);


    if (sb->s_free_inodes_count == 0 || sb->s_free_blocks_count == 0) {
        perror("No free space in disk\n");
        return ENOSPC;
    }

    char *dest_file_name;
    int dest_parent_inode_id = parse_dest_dir(&dest_file_name, dest_path1);
    if (dest_parent_inode_id == -1) {
        exit(1);
    }
    if (dest_file_name == NULL) {
        // get the name of the file we want to copy
        dest_file_name = strtok(source_path, "/");
        char *next = strtok(NULL, "/");
        while (next != NULL) {
            dest_file_name = next;
            next = strtok(NULL, "/");
        }
        if (strlen(dest_file_name) > EXT2_NAME_LEN) {
            perror("the provided file's name is too long");
            return ENOENT;
        }        
    }

    
    // get the size of the file we want to copy    
    float filesize = lseek(source, 0, SEEK_END);
    int block_needed = ceil(filesize/EXT2_BLOCK_SIZE);
    // if the block we need is more than the number of a direct point blocks in an inode
    // then add the number of needed block by one for the indirect pointer block
    if (block_needed > 12) {
        block_needed += 1;
    }

    int need_new_for_entry = check_whether_need_to_get_new_block_for_new_entry(dest_parent_inode_id, strlen(dest_file_name));

    if (block_needed + need_new_for_entry > sb->s_free_blocks_count) {
        perror("not enough space for the new file\n");
        return ENOSPC;
    }
    
    // map the source file
    char *src = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, source, 0);
    if (src == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    int new_inode_id = find_free_in_inode_bitmap();
    if (new_inode_id == -1) {
        perror("no more free inode");
        return ENOSPC;
    }
    new_inode_id = get_free_inode();
    struct ext2_inode * new_inode = GET_INODE(new_inode_id);
    //copy our data into the blocks
    for (int i = 0; i < 12 && i < block_needed; i++) {
        int new_block_id = get_free_block();
        char * block = GET_BLOCK(new_block_id);
        memcpy(block, src + EXT2_BLOCK_SIZE * i, EXT2_BLOCK_SIZE);  
        new_inode->i_block[i] = new_block_id;
    }

    //find a indirection block and allocate blocks to that and copy the rest of the data into it
    if (block_needed > 12) {
        int indirect_block_id = get_free_block();
        new_inode->i_block[12] = indirect_block_id;
        int * indirect_block = (int *)GET_BLOCK(indirect_block_id);
        for (int i = 0; i < block_needed - 13; i++){
            int new_block_id = get_free_block();
            char *block = GET_BLOCK(new_block_id);
            memcpy(block, src + EXT2_BLOCK_SIZE * (i + 12), EXT2_BLOCK_SIZE);
            indirect_block[i] = new_block_id;
        }
    }
    // initialize the new inode
    new_inode->i_mode = EXT2_S_IFREG;
    new_inode->i_uid = 0;
    new_inode->i_size = filesize;
    new_inode->i_blocks = block_needed * 2;
    new_inode->i_links_count = 1;
    new_inode->i_dtime = 0;

    write_new_entry_to_dir(dest_parent_inode_id, new_inode_id, dest_file_name, EXT2_FT_REG_FILE);
    return 0;
}