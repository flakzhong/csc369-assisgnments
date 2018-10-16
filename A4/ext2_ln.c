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
    int s = 0;
    int option;
    char *usage = "Usage: ext2_ln <image file name> [-s] <SOURCE> <LINK>\n";

    // check whether the link we want to creat is a symbolic link
    while ((option = getopt(argc, argv, "s")) != -1) {
        switch (option) {
            case 's':
                s++;
                break;
            default:
                break;
        }
    }

    // check if the given arguments are correct
    if (argc != 4 + s) {
        perror(usage);
        exit(1);
    }

    // create multiple copies for the given source
    int source_len = strlen(argv[2 + s]);
    char source1[source_len + 1];
    strncpy(source1, argv[2+s], source_len);
    char source2[source_len + 1];
    strncpy(source2, argv[2 + s], source_len);

    int link_len = strlen(argv[2 + s]);
    char link1[link_len + 1];
    strncpy(link1, argv[3+s], link_len);
    init_disk(argv[1 + s]);

    // we need to guarentee that the given source is existing in the given disk
    int source_inode_id;
    if (s) {
        // if we need to create symbolic link, then it can link to everything
        source_inode_id = find_inode_by_abs_path(source1, ALL);
    } else {
        // if we need to create a hard link, then it cannot link to directory
        source_inode_id = find_inode_by_abs_path(source1, NOTDIR);
    }
    if (source_inode_id == -2) {
        perror("Cannot find the source file");
        return ENOENT;
    }

    char* link_name;
    int dest_inode_id = parse_dest_dir(&link_name, link1);
    if (link_name == NULL) {
        // get the name of the file we want to creat link
        link_name = strtok(source2, "/");
        char *next = strtok(NULL, "/");
        while (next != NULL) {
            link_name = next;
            next = strtok(NULL, "/");
        }
        if (strlen(link_name) > EXT2_NAME_LEN) {
            perror("the provided file's name is too long");
            return ENOENT;
        } 
    }

    if (s) {
        // if we are required to create a symbolic link
        // create a new file which contains the provided path
        float source_len = strlen(argv[2 + s]);
        if (source_len > 4096) {
            perror("the path of the source is too long");
            return ENOENT;
        }
        int blocks_needed = ceil(source_len/EXT2_BLOCK_SIZE);
        int need_block_for_new_entry = check_whether_need_to_get_new_block_for_new_entry(dest_inode_id, strlen(link_name));
        
        if (blocks_needed + need_block_for_new_entry > sb->s_free_blocks_count) {
            perror("not enough space");
            return ENOSPC;
        } 

        int new_inode_id = find_free_in_inode_bitmap();
        if (new_inode_id == -1) {
            perror("no more free inode for the link");
            return ENOSPC;
        }

        new_inode_id = get_free_inode();
        struct ext2_inode *new_inode = (struct ext2_inode *)GET_INODE(new_inode_id);
        new_inode->i_mode = EXT2_S_IFLNK;
        new_inode->i_links_count = 1;
        new_inode->i_size = source_len;
        new_inode->i_blocks = blocks_needed * 2;
        new_inode->i_dtime = 0;
        for (int i = 0; i < blocks_needed; i++) {
            int new_block_id = get_free_block();
            char* block = GET_BLOCK(new_block_id);
            strncpy(block, argv[2 + s] + (i * EXT2_BLOCK_SIZE), EXT2_BLOCK_SIZE);
            new_inode->i_block[i] = new_block_id;
        }
        write_new_entry_to_dir(dest_inode_id, new_inode_id, link_name, EXT2_FT_SYMLINK);
    } else {
        struct ext2_inode *source_inode = GET_INODE(source_inode_id);
        source_inode->i_links_count ++;
        write_new_entry_to_dir(dest_inode_id, source_inode_id, link_name, EXT2_FT_REG_FILE);        
    }

    return 0;
}