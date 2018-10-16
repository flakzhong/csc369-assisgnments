#include "ext2.h"

#define EXT2_BLOCK_NUM 128
#define GET_BLOCK(x) ((char *)disk + ((x) * EXT2_BLOCK_SIZE))
#define GET_INODE(x) (inode_table + ((x) - 1))


void change_bitmap(char *, int);
int find_free_in_block_bitmap();
int find_free_in_inode_bitmap();

void init_disk(char *);
int search_for_curr_dir(int, char *, int);
int find_parent_by_abs_path(char **, char *);
int find_inode_by_abs_path(char *, int);
int get_entry_size(int);