


#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

typedef struct _FRAME_LIST {
	int frame;
	struct _FRAME_LIST * next;
	struct _FRAME_LIST * pre;
} frame_list;


frame_list* victim_list;
frame_list** ptr_array;
/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {
	int result;
	frame_list* out = victim_list->pre;
        out->pre->next = victim_list;
        victim_list->pre = out->pre;
        result = out->frame;
        ptr_array[result] = NULL;
        free(out);
        
	return result;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {
	int fn = p->frame >> PAGE_SHIFT;
	// no element in the list
	// so we add p to it
	if (victim_list == NULL) {
                
		victim_list = malloc(sizeof(victim_list));
		victim_list->frame = fn;

		victim_list->pre = victim_list;
		victim_list->next = victim_list;
		ptr_array[fn] = victim_list;
	} else {
		// if p is not in the array
                // we allocate space for it and initialize its frame number
		if (ptr_array[fn] == NULL) {
			ptr_array[fn] = malloc(sizeof(frame_list));
                        ptr_array[fn]->pre = NULL;
                        ptr_array[fn]->next = NULL;
		} 
		frame_list* temp = ptr_array[fn];
		temp->frame = fn;
                
                
		// only one element in the list
		// pre == itself
		if (victim_list->next == victim_list && victim_list->pre == victim_list) {
			temp->next = victim_list;
			temp->pre = victim_list;
			victim_list->pre = temp;
			victim_list->next = temp;
                        victim_list = temp;
		} else {
                        
                        frame_list* tail = victim_list->pre;
                        // if p is the tail
                        // just change what victim_list points to
                        if (tail->frame == fn) {
                            victim_list = tail;
                            return;
                        }
                        
                        // if temp is newly added
                        // it won't have a pre or a next
                        // make it the head directly
                        if (temp->pre == NULL) {
                            temp->next = victim_list;
                            temp->pre = tail;
                            tail->next = temp;
                            victim_list->pre = temp;
                            victim_list = temp;

                        // otherwise we change some pointers and move temp to the head
                        } else {
                            temp->pre->next = temp->next;
                            temp->next->pre = temp->pre;
                            temp->next = victim_list;
                            temp->pre = tail;
                            tail->next = temp;
                            victim_list->pre = temp;
                            victim_list = temp;
                        
                        }

		}
	}
	return;
}


/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
	victim_list = NULL;
	ptr_array = malloc(sizeof(frame_list*) * memsize);
	int i;
	for (i = 0; i < memsize; i++) {
		ptr_array[i] = NULL;

	}
}

