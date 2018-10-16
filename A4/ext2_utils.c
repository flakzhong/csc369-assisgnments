#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2_utils.h"
#include "ext2.h"

#define ALL 0
#define DIR 1
#define FILE 2
#define NOTDIR 3

unsigned char* disk;
struct ext2_super_block *sb;
struct ext2_group_desc *bg;
struct ext2_inode *inode_table;
int root_inode;
char *inode_bitmap;
char *block_bitmap;

void init_disk(char *disk_path) {
    int fd = open(disk_path, O_RDWR);
    if(fd < 0) {
        perror("cannot find virtual disk");
        exit(1);
    }
    disk = mmap(NULL, EXT2_BLOCK_NUM * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    sb = (struct ext2_super_block *)(GET_BLOCK(1));
    bg = (struct ext2_group_desc *)(GET_BLOCK(2));
    block_bitmap = (char *)(GET_BLOCK(bg->bg_block_bitmap));
    inode_bitmap = (char *)(GET_BLOCK(bg->bg_inode_bitmap));
    inode_table = (struct ext2_inode *)(GET_BLOCK(bg->bg_inode_table));
    root_inode = 2;
}



int search_for_curr_dir(int curr_inode_id, char* file_name, int mode) {
    if (curr_inode_id == -1) {
        if (mode == DIR || mode == ALL) {
            return root_inode;
        }
        return -1;
    }
    // the value of mode indicates what kind of thing we want to search for
    int inode_found = 0;
    // search all blocks for the current inode.
    struct ext2_inode* curr_inode = (struct ext2_inode*)(GET_INODE(curr_inode_id));
    for(int i = 0; i < 12 && curr_inode->i_block[i] != 0 && inode_found == 0; i++) {
        struct ext2_dir_entry *curr_dir = (struct ext2_dir_entry *)(GET_BLOCK(curr_inode->i_block[i]));
        int k = 0;
        int rec_len = 0;
        while(k < EXT2_BLOCK_SIZE) {
            curr_dir = (struct ext2_dir_entry *)((char *)curr_dir + rec_len);
            rec_len = curr_dir->rec_len;
            k += curr_dir->rec_len;
            if (mode == ALL) {
                if (strncmp(curr_dir->name, file_name, curr_dir->name_len) == 0) {
                    inode_found = 1;
                    return curr_dir->inode;
                }
            } else if (mode == DIR) {
                if (strncmp(curr_dir->name, file_name, curr_dir->name_len) == 0 && curr_dir->file_type == EXT2_FT_DIR) {
                    inode_found = 1;
                    return curr_dir->inode;
                }
            } else if (mode == FILE) {
                if (strncmp(curr_dir->name, file_name, curr_dir->name_len) == 0 && curr_dir->file_type == EXT2_FT_REG_FILE) {
                    inode_found = 1;
                    return curr_dir->inode;
                }
            } else if (mode == NOTDIR) {
                if (strncmp(curr_dir->name, file_name, curr_dir->name_len) == 0 && curr_dir->file_type != EXT2_FT_DIR) {
                    inode_found = 1;
                    return curr_dir->inode;
                }
            }
        }
    }
    return -1;
}

int find_parent_by_abs_path(char** child_dir, char* path) {
    char *curr_lev = strtok(path, "/");
    if (curr_lev == NULL) {
        // if the given directory is the root directory, then just return NULL to indicate there is no parent directory for the given path
        *child_dir = NULL;
        return -1;
    }
    char *next_lev = strtok(NULL, "/");
    int parent_inode = root_inode;
    while (next_lev != NULL) {
        // search parent inode for the current level directory
        parent_inode = search_for_curr_dir(parent_inode, curr_lev, DIR);
        curr_lev = next_lev;
        next_lev = strtok(NULL, "/");
        if (next_lev != NULL && parent_inode == -1) {
            // if we are not able to find a directory on the disk when we are still parsing the given directory, then return an error
            perror("NO ENTRY\n");
            *child_dir = NULL;
            return -2;
        }
    }
    // return all the result of the function
    *child_dir = curr_lev;
    return parent_inode;
}

int find_inode_by_abs_path(char* path, int mode) {
    char * child_dir;
    // find the parent inode of the given directory first
    int parent = find_parent_by_abs_path(&child_dir, path);
    if (parent == -1) {
        return root_inode;
    } else if (parent == -2) {
        // if we are not able to find the parent directory of the current directory
        // return the error
        return -2;
    }
    int result = search_for_curr_dir(parent, child_dir, mode);
    if (result == -1) {
        // if we cannot find the file, then just return error
        return -2;
    }
    return result;
}

int find_free_in_block_bitmap() {
    // find unused block
    for(int j = 0; j < 16; j++) {
        for(int k = 0; k < 8; k++) {
            if((*(block_bitmap + j) >> k & 1) == 0) {
                return 8 * j + k + 1;
            }
        }
    }
    return -1;
}

int find_free_in_inode_bitmap() {
    // find unused block
    for(int j = 0; j < 4; j++) {
        for(int k = 0; k < 8; k++) {
            if((*(inode_bitmap + j) >> k & 1) == 0) {
                return 8 * j + k + 1;
            }
        }
    }
    return -1;
}

int get_free_inode() {
    int result = find_free_in_inode_bitmap();
    if (result != -1) {
        sb->s_free_inodes_count --;
        bg->bg_free_inodes_count --;
        change_bitmap((char *) inode_bitmap, result);
        struct ext2_inode * new_inode = GET_INODE(result);
        // clear these fields
        new_inode->i_uid = 0;
        new_inode->i_links_count = 1;
        new_inode->i_dtime = 0;
        new_inode->osd1 = 0;
        new_inode->i_generation = 0;
        new_inode->i_file_acl = 0;
        new_inode->i_dir_acl = 0;
        new_inode->i_faddr = 0;
    }
    
    return result;
}

int get_free_block() {
    int result = find_free_in_block_bitmap();
    if (result != -1) {
        sb->s_free_blocks_count --;
        bg->bg_free_blocks_count --;
        change_bitmap((char *) block_bitmap, result);
    }
    return result;
}

int get_entry_size(int name_len) {
    int size = sizeof(struct ext2_dir_entry) + sizeof(char) * name_len;
    if (size % 4) {
        return (size / 4 + 1) * 4;
    }
    return size;
}


void change_bitmap(char *bitmap, int position) {
    char byte;
    byte = bitmap[(position - 1) / 8];
    int temp = (position - 1) % 8;
    bitmap[(position - 1) / 8] = byte ^ (1 << temp);
}

int check_bitmap(char *bitmap, int position) {
    char byte = bitmap[(position - 1) / 8];
    int temp = (position - 1) % 8;
    return (byte & (1 << temp)) >> temp;
}

int get_last_used_block_index(int inode_id) {
    int i = 0;
    struct ext2_inode* parent_inode = GET_INODE(inode_id);
    while(i < 12) {
        if(parent_inode->i_block[i] != 0 && parent_inode->i_block[i + 1] == 0) {
            return i;
        }
    }
    return -1;
}

struct ext2_dir_entry* get_last_entry(int inode_id, int last_block_index) {
    if (last_block_index == -1) {
        last_block_index = get_last_used_block_index(inode_id);
    }
    int last_used_block = (GET_INODE(inode_id))->i_block[last_block_index];
    int i = 0;
    struct ext2_dir_entry * curr_dir = (struct ext2_dir_entry *)(GET_BLOCK(last_used_block));
    while(i < 1024) {
        i += curr_dir->rec_len;
        // tail located
        if(i == 1024) {
            return curr_dir;
        }
        curr_dir = (struct ext2_dir_entry *)((char *)curr_dir + curr_dir->rec_len);
    }
    return NULL;
}

int check_whether_need_to_get_new_block_for_new_entry(int inode_id, int name_len) {
    int last_block_index = get_last_used_block_index(inode_id);
    struct ext2_dir_entry* last_entry = get_last_entry(inode_id, last_block_index);
    int space_ava = last_entry->rec_len - get_entry_size(last_entry->name_len);
    if (space_ava > get_entry_size(name_len)) {
        return 0;
    }
    return 1;
}

int write_new_entry_to_dir(int parent_inode_id, int new_inode, char* name, unsigned char file_type) {
    if (strlen(name) > EXT2_NAME_LEN) {
        perror("name too long");
        return ENOENT;
    }
    int last_block_index = get_last_used_block_index(parent_inode_id);
    struct ext2_dir_entry* last_entry = get_last_entry(parent_inode_id, last_block_index);
    int space_needed = get_entry_size(strlen(name));
    int last_entry_size = get_entry_size(last_entry->name_len);
    struct ext2_dir_entry *curr_dir;
    int space_ava = last_entry->rec_len - last_entry_size;
    // if current block has no space
    if (space_ava < space_needed) {
        // we try to find a new block
        int new_block_id = find_free_in_block_bitmap();
        if (new_block_id == -1) {
            perror("no more blocks available");
            return ENOSPC;
        }
        new_block_id = get_free_block();
        struct ext2_inode* parent_inode = GET_INODE(parent_inode_id);
        struct ext2_dir_entry * new_entry = (struct ext2_dir_entry *)GET_BLOCK(new_block_id);
        parent_inode->i_block[last_block_index + 1] = new_block_id;
        strncpy(new_entry->name, name, EXT2_NAME_LEN);
        new_entry->inode = new_inode;
        new_entry->file_type = file_type;
        new_entry->name_len = strlen(name);
        new_entry->rec_len = EXT2_BLOCK_SIZE;
        return 1;
    } else {
        // if the block has space
        int temp = last_entry->rec_len;
        last_entry->rec_len = last_entry_size;
        curr_dir = (struct ext2_dir_entry *)((char*)last_entry + last_entry_size);
        curr_dir->inode = new_inode;
        curr_dir->file_type = file_type;
        curr_dir->name_len = strlen(name);
        curr_dir->rec_len = temp - last_entry_size;
        strcpy(curr_dir->name, name);
        return 1;
    }
}

int parse_dest_dir(char ** dest_name, char * path) {
    // this function will be used in ln and cp, it will take in an destination path, and will return the parent inode id of the
    // destination and the name of the destination to the dest_name pointer
    char* temp;
    // get the second last level of the provided directory
    int dest_parent = find_parent_by_abs_path(&temp, path);
    if (dest_parent == -2) {
        perror("provided destination do not exit");
        return -1;
    } 
    // find whether the provided directory has already exists
    int last_level = search_for_curr_dir(dest_parent, temp, ALL);
    if (last_level != -1) {
        // check whether the given destination is a directory
        if(((struct ext2_inode *)GET_INODE(last_level))->i_mode != EXT2_S_IFDIR) {
            // if it is not a directory
            perror("the given destination file has already existed");
            return -1;
        }
        *dest_name = NULL;
        return last_level;
    } else {
        // if we are not able to find the destination in the parent directory
        *dest_name = temp;
        return dest_parent;
    }
}