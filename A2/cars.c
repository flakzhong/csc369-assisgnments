#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
    int i;
    for (i = 0; i < 4; i++) {
        isection.quad[i] = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        isection.lanes[i].buffer = malloc(sizeof(struct car*) * LANE_LENGTH); 
        isection.lanes[i].lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        isection.lanes[i].producer_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
        isection.lanes[i].consumer_cv = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
        isection.lanes[i].capacity = LANE_LENGTH;
        isection.lanes[i].inc = 0;
        isection.lanes[i].passed = 0;
        isection.lanes[i].head = 0;
        isection.lanes[i].tail = 9;
        isection.lanes[i].in_buf = 0;
    }
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;
    // sync
    pthread_mutex_lock(&(l->lock));
    while (l->inc > 0) {
        // we can't move from in_cars to the buffer if the buffer is full
        // so we wait until available space
        while (l->in_buf == l->capacity) {
            pthread_cond_wait(&(l->consumer_cv), &(l->lock));
        }
        // update in_cars info
        // the second element will be the head.
        struct car *curr = l->in_cars;
        l->in_cars = curr->next;
        // insert the car to the buffer and update tail info at the same time
        if(l->tail != 9) {
            (l->buffer)[l->tail + 1] = curr;
            (l->tail)++;
        } else {
            (l->buffer)[0] = curr;
            (l->tail) = 0;
        }
        // update other lane info
        (l->in_buf)++;
        (l->inc)--;
        pthread_cond_signal(&(l->producer_cv));
    }
    pthread_mutex_unlock(&(l->lock));
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
    struct lane *l = arg;
    struct car *head;
    // keep crossing until there is no car in the lane.
    while (l->in_buf != 0 || l->inc != 0) {
        pthread_mutex_lock(&(l->lock));
        while (l->in_buf == 0) {
            pthread_cond_wait(&(l->producer_cv), &(l->lock));
        }
        // lock quadrants.
        head = (l->buffer)[l->head];
        int *quads = compute_path(head->in_dir, head->out_dir);
        int i = 0;
        while (i < 4) {
            if (quads[i] != -1) {
                pthread_mutex_lock(&(isection.quad[quads[i] - 1]));
            }
            i++;
        }
        // update out_dir lane info
        // we don't need to lock the out lane because if any other lane wants to do the same thing
        // they have to lock 4 quads before doing so
        // but we have locked the quads
        struct lane* out_lane = &(isection.lanes[head->out_dir]);
        head->next = out_lane->out_cars;
        out_lane->out_cars = head;
        out_lane->passed++;
        
        // car has crossed, print info.
        printf("%d %d %d\n", head->in_dir, head->out_dir, head->id);
        // update in_dir lane info
        l->in_buf--;
        if (l->head != 9) {
            l->head++;
        } else {
            l->head = 0;
        }
        // unlock the quadrants
        i = 0;
        while (i < 4) {
            if (quads[i] != -1) {
                pthread_mutex_unlock(&(isection.quad[quads[i] - 1]));
            }
            i++;
        }
        free(quads);
        pthread_cond_signal(&(l->consumer_cv));
        pthread_mutex_unlock(&(l->lock));

    }
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    int *dir = malloc(4 * sizeof(int));
    dir[0] = -1;
    dir[1] = -1;
    dir[2] = -1;
    dir[3] = -1;
    if (in_dir == NORTH && out_dir == NORTH) {
        dir[0] = 1;
        dir[1] = 2;
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == NORTH && out_dir == SOUTH) {
        dir[1] = 2;
        dir[2] = 3;
    } else if (in_dir == NORTH && out_dir == WEST) {
        dir[1] = 2;
    } else if (in_dir == NORTH && out_dir == EAST) {
        dir[1] = 2;
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == SOUTH && out_dir == NORTH) {
        dir[0] = 1;
        dir[3] = 4;
    } else if (in_dir == SOUTH && out_dir == SOUTH) {
        dir[0] = 1;
        dir[1] = 2;
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == SOUTH && out_dir == WEST) {
        dir[0] = 1;
        dir[1] = 2;
        dir[3] = 4;
    } else if (in_dir == SOUTH && out_dir == EAST) {
        dir[3] = 4;
    } else if (in_dir == WEST && out_dir == NORTH) {
        dir[0] = 1;
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == WEST && out_dir == SOUTH) {
        dir[2] = 3;
    } else if (in_dir == WEST && out_dir == WEST) {
        dir[0] = 1;
        dir[1] = 2;
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == WEST && out_dir == EAST) {
        dir[2] = 3;
        dir[3] = 4;
    } else if (in_dir == EAST && out_dir == NORTH) {
        dir[0] = 1;
    } else if (in_dir == EAST && out_dir == SOUTH) {
        dir[0] = 1;
        dir[1] = 2;
        dir[2] = 3;
    } else if (in_dir == EAST && out_dir == WEST) {
        dir[0] = 1;
        dir[1] = 2;
        dir[3] = 4;
    } else if (in_dir == EAST && out_dir == EAST) {
        dir[0] = 1;
        dir[1] = 2;
        dir[2] = 3;
        dir[3] = 4;
    }
    return dir;
}