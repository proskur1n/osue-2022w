/**
 * @author Andrey Proskurin 12122381
 * @date   2022-10-21
 * @brief  Accepts graph coloring solutions from the generators and prints the
 *         best ones to the stdout.
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
#include <signal.h>
#include <limits.h>
#include "common.h"

char const *argv0;
volatile sig_atomic_t quit = 0;

/**
 * sigaction sa_handler
 */
static void set_quit_flag(int signal)
{
	(void) signal;
	quit = 1;
}

/**
 * Prints an error message to the stderr.
 */
static void complain(char const *msg)
{
	fprintf(stderr, "[%s]: %s: %s\n", argv0, msg, strerror(errno));
}

/**
 * Returns a new mmap'ed shared memory with the specified name or NULL
 * on failure.
 *
 * @param name Name of the shared memory
 */
static struct shared *create_shared_memory(char const *name)
{
	int fd = -1;
	struct shared *shm = MAP_FAILED;

	fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0) {
		complain("shm_open");
		goto error;
	}

	if (ftruncate(fd, sizeof(struct shared)) < 0) {
		complain("ftruncate");
		goto error;
	}

	shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED) {
		complain("mmap");
		goto error;
	}

	close(fd);
	return shm;
error:
	if(shm != MAP_FAILED) {
		munmap(shm, sizeof(*shm));
	}
	if (fd >= 0) {
		close(fd);
		shm_unlink(name);
	}
	return NULL;
}

/**
 * Returns a new named semaphore or NULL on failure.
 *
 * @param name Name of the semaphore
 * @param init Initial value of the semaphore
 */
static sem_t *create_semaphore(char const *name, int init)
{
	sem_t *sem = sem_open(name, O_CREAT | O_EXCL, 0600, init);
	if (sem == SEM_FAILED) {
		complain("sem_open");
		return NULL;
	} else {
		return sem;
	}
}

/**
 * Closes and unlinks a semaphore previously returned by create_semaphore.
 * It is safe to pass NULL for sem.
 *
 * @param sem  An existing semaphore or NULL
 * @param name Name of the semaphore sem
 */
static void destroy_semaphore(sem_t *sem, char const *name)
{
	if (sem != NULL) {
		if (sem_close(sem) < 0) {
			complain("sem_close");
		}
		if (sem_unlink(name) < 0) {
			complain("sem_unlink");
		}
	}
}

int main(int argc, char **argv)
{
	argv0 = argc > 0 ? argv[0] : "supervisor";
	if (argc > 1) {
		fprintf(stderr, "Usage: %s\n", argv0);
		return EXIT_FAILURE;
	}

	struct sigaction sa = { .sa_handler = set_quit_flag };
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	int status = EXIT_SUCCESS;
	struct shared *shm = NULL;
	sem_t *sem_free = NULL;
	sem_t *sem_used = NULL;
	sem_t *sem_mutex = NULL;

	if (
		(shm = create_shared_memory(SHM_PATH)) == NULL ||
		(sem_free = create_semaphore(SEM_FREE_PATH, MAX_QUEUE_SIZE)) == NULL ||
		(sem_used = create_semaphore(SEM_USED_PATH, 0)) == NULL ||
		(sem_mutex = create_semaphore(SEM_MUTEX_PATH, 1)) == NULL
	) {
		goto error;
	}

	int best_solution = INT_MAX;

	while (!quit) {
		if (sem_wait(sem_used) < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				complain("sem_wait");
				goto error;
			}
		}

		int size = shm->solution_size[shm->rd];
		if (size == 0) {
			printf("[%s] The graph is 3-colorable!\n", argv0);
			break;
		}
		if (size < best_solution) {
			best_solution = size;
			printf("[%s] Solution with %d edges:", argv0, size);
			for (int i = 0; i < size; ++i) {
				printf(" %d-%d", shm->queue[shm->rd][i].first, shm->queue[shm->rd][i].second);
			}
			printf("\n");
		}
		shm->rd = (shm->rd + 1) % MAX_QUEUE_SIZE;

		if (sem_post(sem_free) < 0) {
			complain("sem_post");
			goto error;
		}
	}

cleanup:
	if (sem_free != NULL && shm != NULL) {
		// Signal all generators to quit.
		shm->quit = 1;
		for (int i = 0; i < MAX_QUEUE_SIZE; ++i) {
			sem_post(sem_free);
		}
	}
	destroy_semaphore(sem_mutex, SEM_MUTEX_PATH);
	destroy_semaphore(sem_used, SEM_USED_PATH);
	destroy_semaphore(sem_free, SEM_FREE_PATH);
	if (shm != NULL) {
		if (munmap(shm, sizeof(*shm)) < 0) {
			complain("munmap");
			status = EXIT_FAILURE;
		}
		if (shm_unlink(SHM_PATH) < 0) {
			complain("shm_unlink");
			status = EXIT_FAILURE;
		}
	}
	return status;
error:
	status = EXIT_FAILURE;
	goto cleanup;
}
