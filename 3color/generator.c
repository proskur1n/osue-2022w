/**
 * @author Andrey Proskurin 12122381
 * @date   2022-10-21
 * @brief  Generates graph coloring solution for the supervisor.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include "common.h"

char const *argv0;

/**
 * Prints an error message to stderr.
 */
static void print_error(char const *msg)
{
	fprintf(stderr, "[%s]: %s: %s\n", argv0, msg, strerror(errno));
}

/**
 * Opens and mmaps's an existing shared memory with the specified name. Do not
 * forget to call munmap(3) afterwards.
 *
 * @param name Name of an existing shared memory
 */
static struct shared *open_shared_memory(char const *name)
{
	int fd = shm_open(name, O_RDWR, 0);
	if (fd < 0) {
		print_error("shm_open");
		return NULL;
	}
	struct shared *shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED) {
		print_error("mmap");
		close(fd);
		return NULL;
	}
	close(fd);
	return shm;
}

/**
 * Opens an existing named semaphore.
 *
 * @param name Name of the semaphore
 */
static sem_t *open_semaphore(char const *name)
{
	sem_t *sem = sem_open(name, 0);
	if (sem == SEM_FAILED) {
		print_error("sem_open");
		return NULL;
	} else {
		return sem;
	}
}

/**
 * Closes a semaphore previously returned by open_semaphore.
 * It is safe to pass NULL for sem.
 *
 * @param sem An existing semaphore or NULL
 */
static void close_semaphore(sem_t *sem)
{
	if (sem != NULL && sem_close(sem) < 0) {
		print_error("sem_close");
	}
}

int main(int argc, char *argv[])
{
	argv0 = argc > 0 ? argv[0] : "generator";
	if (argc < 2) {
		fprintf(stderr, "Usage: %s edge1...\n", argv0);
		return EXIT_FAILURE;
	}

	int status = EXIT_SUCCESS;
	struct edge *edges = NULL;
	color_t *nodes = NULL;
	struct shared *shm = NULL;
	sem_t *sem_free = NULL;
	sem_t *sem_used = NULL;
	sem_t *sem_mutex = NULL;

	int num_edges = argc - 1;
	int max_node_name = 0;
	edges = calloc(num_edges, sizeof(struct edge));
	if (edges == NULL) {
		print_error("calloc");
		goto error;
	}
	for (int i = 1; i < argc; ++i) {
		int first = -1;
		int second = -1;
		if (sscanf(argv[i], "%d-%d", &first, &second) != 2 || first < 0 || second < 0) {
			fprintf(stderr, "[%s]: edges must have the format a-b\n", argv0);
			goto error;
		}
		if (first > max_node_name) {
			max_node_name = first;
		}
		if (second > max_node_name) {
			max_node_name = second;
		}
		edges[i - 1] = (struct edge) {first, second};
	}
	nodes = calloc(max_node_name + 1, sizeof(color_t));
	if (nodes == NULL) {
		print_error("calloc");
		goto error;
	}

	if (
		(shm = open_shared_memory(SHM_PATH)) == NULL ||
		(sem_free = open_semaphore(SEM_FREE_PATH)) == NULL ||
		(sem_used = open_semaphore(SEM_USED_PATH)) == NULL ||
		(sem_mutex = open_semaphore(SEM_MUTEX_PATH)) == NULL
	) {
		goto error;
	}

	srand(getpid());

	while (!shm->quit) {
		struct edge solution[MAX_BAD_EDGES];
		int solution_size = 0;
		for (int i = 0; i <= max_node_name; ++i) {
			// Assign a random color to each node.
			nodes[i] = rand() % NCOLORS;
		}
		for (int i = 0; i < num_edges; ++i) {
			if (nodes[edges[i].first] == nodes[edges[i].second]) {
				if (solution_size >= MAX_BAD_EDGES) {
					// This solution is too big.
					solution_size = -1;
					break;
				}
				// Remove this edge since it connects two nodes of the same color.
				solution[solution_size++] = edges[i];
			}
		}
		if (solution_size < 0) {
			// Could not find a small enough solution. Try again.
			continue;
		}

		if (sem_wait(sem_mutex) < 0) {
			if (errno == EINTR) {
				continue;
			}
			print_error("sem_wait");
			goto error;
		}
		if (sem_wait(sem_free) < 0) {
			if (errno == EINTR) {
				if (sem_post(sem_mutex) < 0) {
					print_error("sem_post");
					goto error;
				}
				continue;
			}
			print_error("sem_wait");
			sem_post(sem_mutex);
			goto error;
		}

		// Write the new solution to the queue.
		for (int i = 0; i < solution_size; ++i) {
			shm->queue[shm->wr][i] = solution[i];
		}
		shm->solution_size[shm->wr] = solution_size;
		shm->wr = (shm->wr + 1) % MAX_QUEUE_SIZE;

		if (sem_post(sem_mutex) < 0 || sem_post(sem_used) < 0) {
			print_error("sem_post");
			goto error;
		}
	}

cleanup:
	close_semaphore(sem_mutex);
	close_semaphore(sem_used);
	close_semaphore(sem_free);
	if (shm != NULL && munmap(shm, sizeof(*shm)) < 0) {
		print_error("munmap");
		status = EXIT_FAILURE;
	}
	free(nodes);
	free(edges);
	return status;
error:
	status = EXIT_FAILURE;
	goto cleanup;
}
