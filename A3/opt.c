#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"

#define MAXLINE 256
#ifdef TRACE_64
// User-level virtual addresses on 64-bit Linux system are 36 bits in our traces
// and the page size is still 4096 (12 bits). 
// We split the remaining 24 bits evenly into top-level (page directory) index
// and second-level (page table) index, using 12 bits for each. 
#define LEVEL_SIZE          64    // Leaves just top 12 bits of vaddr 
#define BIT_PER_LEVEL        6

#else // TRACE_32
// User-level virtual addresses on 32-bit Linux system are 32 bits, and the 
// page size is still 4096 (12 bits).
// We split the remaining 20 bits evenly into top-level (page directory) index
// and second level (page table) index, using 10 bits for each.
#define LEVEL_SIZE        32     // Leaves just top 10 bits of vaddr 
#define BIT_PER_LEVEL      5
#endif

#define FIRST_LEVEL_INDEX(x)  ((x) & (LEVEL_SIZE - 1))
#define SECOND_LEVEL_INDEX(x)  (((x) >> BIT_PER_LEVEL) & (LEVEL_SIZE - 1))
#define THIRD_LEVEL_INDEX(x) (((x) >> (BIT_PER_LEVEL * 2)) & (LEVEL_SIZE - 1))
#define FOURTH_LEVEL_INDEX(x) (((x) >> (BIT_PER_LEVEL * 3)) & (LEVEL_SIZE - 1))
extern int memsize;

extern int debug;

extern struct frame *coremap;

extern char* tracefile;


typedef struct _FRAME_LIST {
	int frame;
	struct _FRAME_LIST * next;
} frame_list;

frame_list* frame_ava;
void frame_init() {
	frame_ava = calloc(1, sizeof(frame_list));
	frame_list *curr = frame_ava;
	frame_ava->frame = 0;
	for (int i = 1; i < memsize; i++) {
		curr->next = calloc(1, sizeof(frame_list));
		curr = curr->next;
		curr->frame = i;
	}
}

frame_list* get_frame() {
	frame_list* result = frame_ava;
	frame_ava = frame_ava->next;
	return result;
}

void return_frame(frame_list* frame) {
	int frame_num = frame->frame;
	if (frame_num < frame_ava->frame) {
		// if the returned frame should be at the head of the frame list
		frame->next = frame_ava;
		frame_ava = frame;
		return;
	}
	frame_list* curr;
	for (curr = frame_ava; curr->next != NULL || curr->next->frame < frame_num; curr = curr->next) {}
	frame->next = curr->next;
	curr->next = frame;
	return;
}

typedef struct _LINK_LIST{
	unsigned int entry;
	int ref_index;
	int next_ref;
	int mem_heap_index;
	struct _LINK_LIST *next;
    struct _LINK_LIST *next_trace;
	frame_list* frame;
} trace_node;

trace_node* trace_list_head;
trace_node *****page_dict;
void insert_to_dict(trace_node* new_node) {
	unsigned int entry = new_node->entry;
	unsigned int ref_ind = new_node->ref_index;
	int first_ind = FIRST_LEVEL_INDEX(entry);
	int second_ind = SECOND_LEVEL_INDEX(entry);
	int third_ind = THIRD_LEVEL_INDEX(entry);
	int fourth_ind = FOURTH_LEVEL_INDEX(entry);
	if (!page_dict[first_ind]) {
		page_dict[first_ind] = calloc(LEVEL_SIZE, sizeof(trace_node***));
	}
	if (!page_dict[first_ind][second_ind]) {
		page_dict[first_ind][second_ind] = calloc(LEVEL_SIZE, sizeof(trace_node**));
	}
	if (!page_dict[first_ind][second_ind][third_ind]) {
		page_dict[first_ind][second_ind][third_ind] = calloc(LEVEL_SIZE, sizeof(trace_node*));
	}
	if (!page_dict[first_ind][second_ind][third_ind][fourth_ind]) {
		page_dict[first_ind][second_ind][third_ind][fourth_ind] = new_node;
	} else {
		trace_node* curr = page_dict[first_ind][second_ind][third_ind][fourth_ind];
		while(curr->next != NULL){curr = curr->next;}
		curr->next_ref = ref_ind;
		curr->next = new_node;
	}
	return;
}

trace_node* get_from_dict(unsigned int entry) {
	int first_ind = FIRST_LEVEL_INDEX(entry);
	int second_ind = SECOND_LEVEL_INDEX(entry);
	int third_ind = THIRD_LEVEL_INDEX(entry);
	int fourth_ind = FOURTH_LEVEL_INDEX(entry);
	trace_node * result = page_dict[first_ind][second_ind][third_ind][fourth_ind];
	return result;
}

void pop_entry(unsigned int entry) {
	// this function will pop the first element in the link list at the given entry
	int first_ind = FIRST_LEVEL_INDEX(entry);
	int second_ind = SECOND_LEVEL_INDEX(entry);
	int third_ind = THIRD_LEVEL_INDEX(entry);
	int fourth_ind = FOURTH_LEVEL_INDEX(entry);
	trace_node * head = page_dict[first_ind][second_ind][third_ind][fourth_ind];
	page_dict[first_ind][second_ind][third_ind][fourth_ind] = head->next;
	return;
}

trace_node ** mem_heap;
int mem_heap_ava;
int geq(trace_node* first, trace_node* second) {
	// this function is the logic of greater or equal to in the mem_heap
	int first_next_ref = first->next_ref;
	if (first_next_ref < 0) {
		return 1;
	}
	int second_next_ref = second->next_ref;
	if (second_next_ref < 0) {
		return 0;
	}
	if (first_next_ref >= second_next_ref) {
		return 1;
	}
	return 0;
}
void bubble_up(int index) {
	// this function is used for bubbuling up the item at given index in the mem_heap
	if (index == 0) {
		// if the current item has already at the top of the mem_heap, then no bubble up is needed
		return;
	}
	int parent_index = (index + 1)/2 - 1;
	if (geq(mem_heap[parent_index], mem_heap[index])) {
		// if the parent node is bigger than the current node, then no need to continue bubble up
		return;
	}
	// swap the current node with the parent node
	trace_node* temp;
	temp = mem_heap[parent_index];
	mem_heap[parent_index] = mem_heap[index];
	mem_heap[index] = temp;
	mem_heap[parent_index]->mem_heap_index = parent_index;
	mem_heap[index]->mem_heap_index = index;
	// call the bubble up function recursively
	bubble_up(parent_index);
}
void bubble_down(int index) {
	// this function is used for bubbling down the item at the given index in the mem_heap
	if (index >= memsize + 1/2) {
		// if the curret node has already at the button of the mem_heap, then no bubble down is needed
		return;
	}
	int child1 = index * 2 + 1;
	int child2 = index * 2 + 2;
	// pick the child with the larger index to do the swap
	int larger_child;
	trace_node * larger_child_node;
	if (child2 >= memsize || geq(mem_heap[child1], mem_heap[child2])) {
		// if the index of the second child is not in the heap
		// or child1 is greater or equal to child2, pick child1 as larger child
		larger_child = child1;
		larger_child_node = mem_heap[child1];
	} else {
		larger_child = child2;
		larger_child_node = mem_heap[child2];
	}
	// if the larger child node is not bigger than the current node, then do not need to bubble down
	if (geq(mem_heap[index], mem_heap[larger_child])) {
		return;
	}
	mem_heap[larger_child] = mem_heap[index];
	mem_heap[index] = larger_child_node;
	mem_heap[larger_child]->mem_heap_index = larger_child;
	mem_heap[index]->mem_heap_index = index;
	// call the bubble down function recursively
	bubble_down(larger_child);
	return;
}

void insert_into_mem(trace_node* node) {
	// this function is used for inserting a item into the mem_heap
	// this function should only work when there are space available in the mem_heap
	if (mem_heap_ava != 0) {
		int insert_index = memsize - mem_heap_ava;
		mem_heap[insert_index] = node;
		mem_heap[insert_index]->mem_heap_index = insert_index;
		bubble_up(insert_index);
		mem_heap_ava--;
	}
	return;
}

trace_node* get_max_from_mem() {
	// this function is used for getting the item with the greatest item in the mem_heap
	// this function should only be called when the mem_heap is full as mem_heap is a representation of physical memory
	trace_node* result = NULL;
	if (mem_heap_ava == 0) {
		result = mem_heap[0];
		pop_entry(result->entry);
		return_frame(result->frame);
		trace_node* tail_item = mem_heap[memsize - 1];
		mem_heap[memsize - 1] = NULL;
		tail_item->mem_heap_index = 0;
		mem_heap[0] = tail_item;
		bubble_down(0);
		mem_heap_ava++;
	}
	return result;
}


frame_list* victim_list;
frame_list* victim_list_tail;
void run_algorithm() {
    trace_node* curr = trace_list_head;
    while (curr->next_trace != NULL) {
        // if the current trace node is the last trace node in the linked-list, then end the algorithm
        unsigned int entry = curr->entry;
        curr = curr->next_trace;
        trace_node *node = get_from_dict(entry);
        // if we get a hit in the physical memory
        if (node->mem_heap_index != -1) {
            // let the new trace node get the memory frame from the original trace node
            trace_node *new_node = node->next;
            new_node->frame = node->frame;
            // pop the original trace node from the dict
            pop_entry(node->entry);
            // update the original trace node in the mem_heap
            new_node->mem_heap_index = node->mem_heap_index;
            mem_heap[new_node->mem_heap_index] = new_node;
            // bubble up and bubble down the updated node to adjust its location in mem_heap
            bubble_down(new_node->mem_heap_index);
            bubble_up(new_node->mem_heap_index);
            // free the original trace node
            free(node);
            continue;
        }
        if (mem_heap_ava) {
            // if there are still space available in the mem_heap, then adding trace node into the mem_heap
            node->frame = get_frame();
            insert_into_mem(node);
            continue;
        }
        // if there are no more space available, pick the victim with the furthest use
        trace_node* victim = get_max_from_mem();
        // pop the victim from the dict
        pop_entry(victim->entry);
        // get the frame number of the victim
        frame_list* new_victim = malloc(sizeof(frame_list));
        new_victim->frame = victim->frame->frame;
        if (victim_list == NULL) {
            victim_list = new_victim;
            victim_list_tail = new_victim;
        } else {
            victim_list_tail->next = new_victim;
            victim_list_tail = victim_list_tail->next;
        }
        // return the frame of the victim
        return_frame(victim->frame);
        // free the victim
        free(victim);
        continue;
    }
}

/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {
    int result = victim_list->frame;
    victim_list = victim_list->next;
	return result;
}


/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {
	return;
}



/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
    victim_list = NULL;
	mem_heap_ava = memsize;
	page_dict = calloc(LEVEL_SIZE, sizeof(trace_node****));
	mem_heap = calloc(memsize, sizeof(trace_node*));
	frame_init();
	FILE *tfp = stdin;
	if(tracefile != NULL) {
		if((tfp = fopen(tracefile, "r")) == NULL) {
			perror("Error opening tracefile:");
			exit(1);
		}
	}
	char buf[MAXLINE];
	addr_t vaddr = 0;
	char type;
	int index = 0;
	trace_list_head = malloc(sizeof(trace_node));
	trace_node* curr = trace_list_head;
	while(fgets(buf, MAXLINE, tfp) != NULL) {
		if(buf[0] != '=') {
			sscanf(buf, "%c %lx", &type, &vaddr);
			unsigned int entry = vaddr >> PAGE_SHIFT;
			// initialize the trace node
			curr->frame = NULL;
			curr->mem_heap_index = -1;
			curr->ref_index = index;
			curr->entry = entry;
			curr->next_ref = -1;
			curr->next = NULL;
			curr->next_trace = malloc(sizeof(trace_node));
			// add the trace node to the dict
			insert_to_dict(curr);
			// move the current cursor to the next trace node
			curr = curr->next_trace;
			index++;
		} else {
			continue;
		}
	}
    curr->next_trace = NULL;
    run_algorithm();
}