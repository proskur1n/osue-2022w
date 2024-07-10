/**
 * @author Andrey Proskurin 12122381
 * @date   2022-10-21
 * @brief  This header is shared between supervisor.c and generator.c
 */
#ifndef COMMON_H
#define COMMON_H

#define SHM_PATH "/12122381_shm"
#define MAX_BAD_EDGES 12
#define MAX_QUEUE_SIZE 32
#define SEM_FREE_PATH "/12122381_free"
#define SEM_USED_PATH "/12122381_used"
#define SEM_MUTEX_PATH "/12122381_mutex"
#define NCOLORS 3

/**
 * An unsigned integer type big enough to hold NCOLORS different values.
 */
typedef unsigned char color_t;

/**
 * An undirected edge between the 'first' and 'second' nodes.
 */
struct edge {
	int first;
	int second;
};

/**
 * Layout of the memory buffer shared between the supervisor and the generators.
 */
struct shared {
	struct edge queue[MAX_QUEUE_SIZE][MAX_BAD_EDGES];
	int solution_size[MAX_QUEUE_SIZE];
	int wr; // Read-end of the queue
	int rd; // Write-end of the queue
	int quit; // Notifies the generators to quit.
};

#endif // COMMON_H
