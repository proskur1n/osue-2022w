/**
 * @author Andrey Proskurin 12122381
 * @date   2022-10-26
 * @brief  Parallel implementation of mergesort with subprocesses and pipes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <locale.h>

#define READ_END 0
#define WRITE_END 1

/**
 * Prints an error message followed by errno information to stderr.
 */
#define logger(msg) fprintf(stderr, "[%s:%d (%d)] %s: %s\n", argv0, __LINE__, getpid(), msg, strerror(errno))

/**
 * Represents a single child process with its input and output streams.
 */
struct child {
	FILE *in;
	FILE *out;
};

static char const *argv0;
#ifdef OSUETREE
static char *substep = NULL;
static long substep_length = 0;
static int num_lines[2];
#endif

/**
 * Tries to close the file descriptor fd and prints an error message if this
 * operation fails. Calling try_close with a negaitive fd is a no-op.
 *
 * @param fd File descriptor to close
 */
static void try_close(int fd)
{
	if (fd >= 0 && close(fd) < 0) {
		logger("close");
	}
}

/**
 * Calls fclose(3) on the given stream and sets it to NULL. Passing NULL for
 * stream is a no-op. Returns 0 on success and a negative value on failure.
 *
 * @param streamptr Pointer to a valid stream or pointer to NULL
 */
static int close_stream(FILE **streamptr)
{
	FILE *file = *streamptr;
	*streamptr = NULL;
	if (file != NULL && fclose(file) == EOF) {
		logger("fclose");
		return -1;
	}
	return 0;
}

/**
 * Enables the FD_CLOEXEC flag for the given file descriptor. Does not change
 * or unset other flags. Returns 0 on success and -1 on failure.
 *
 * @param fd File descriptor
 */
static int set_cloexec(int fd)
{
	int current = fcntl(fd, F_GETFD);
	if (current == -1) {
		logger("fcntl getfd");
		return -1;
	}
	if (fcntl(fd, F_SETFD, current | FD_CLOEXEC) == -1) {
		logger("fcntl setfd");
		return -1;
	}
	return 0;
}

/**
 * Creates a new unnamed pipe with the FD_CLOEXEC flag enabled for both of its
 * ends. Returns 0 on success and -1 on failure.
 *
 * @param fildes Write and read ends of the new pipe
 */
static int pipe_cloexec(int fildes[2])
{
	if (pipe(fildes) < 0) {
		logger("pipe");
		return -1;
	}
	if (set_cloexec(fildes[0]) < 0 || set_cloexec(fildes[1]) < 0) {
		try_close(fildes[0]);
		try_close(fildes[1]);
		return -1;
	}
	return 0;
}

/**
 * Creates a new instance of this program and sets the first parameter to the
 * stdin and stdout of the new child process. Returns 0 on success and a negaive
 * value on failure.
 *
 * @param child Used to return information about the new child.
 */
static int fork_child(struct child *child)
{
	int tochild[2];
	int fromchild[2];
	if (pipe_cloexec(tochild) < 0) {
		return -1;
	}
	if (pipe_cloexec(fromchild) < 0) {
		try_close(tochild[0]);
		try_close(tochild[1]);
		return -1;
	}

	switch (fork()) {
	case -1:
		logger("fork");
		try_close(tochild[READ_END]);
		try_close(tochild[WRITE_END]);
		try_close(fromchild[READ_END]);
		try_close(fromchild[WRITE_END]);
		return -1;
	case 0:
		// Child process
		if (dup2(tochild[READ_END], STDIN_FILENO) < 0 || dup2(fromchild[WRITE_END], STDOUT_FILENO) < 0) {
			logger("dup2");
			exit(EXIT_FAILURE);
		}
		// Unused pipe ends will get closed by FD_CLOEXEC.
		execlp(argv0, argv0, NULL);
		logger("execlp");
		exit(EXIT_FAILURE);
	default:
		// Parent / original process
		try_close(tochild[READ_END]);
		try_close(fromchild[WRITE_END]);
		FILE *in = fdopen(tochild[WRITE_END], "w");
		if (in == NULL) {
			logger("fdopen");
			try_close(tochild[WRITE_END]);
			try_close(fromchild[READ_END]);
			return -1;
		}
		FILE *out = fdopen(fromchild[READ_END], "r");
		if (out == NULL) {
			logger("fdopen");
			close_stream(&in);
			try_close(fromchild[READ_END]);
			return -1;
		}
		child->in = in;
		child->out = out;
		return 0;
	}

	assert(0 && "unreachable");
}

/**
 * Simple wrapper around getline(3) that doesn't include the line delimiter into
 * the resulting string. Same arguments as getline(3).
 */
static ssize_t getline_no_newline(char **lineptr, size_t *n, FILE *stream)
{
	ssize_t len = getline(lineptr, n, stream);
	if (len < 0 && !feof(stream)) {
		logger("getline");
	}
	if (len > 0 && (*lineptr)[len - 1] == '\n') {
		--len;
		(*lineptr)[len] = '\0';
	}
	return len;
}

/**
 * Merges the lines coming from first_stream and second_stream in an
 * alphabetical order and prints them to the stdout. Returns 0 on success and
 * a negative number on failure.
 */
static int merge(FILE *first_stream, FILE *second_stream)
{
	char *line1 = NULL;
	char *line2 = NULL;
	size_t cap1 = 0;
	size_t cap2 = 0;
	ssize_t len1 = getline_no_newline(&line1, &cap1, first_stream);
	ssize_t len2 = getline_no_newline(&line2, &cap2, second_stream);

#ifdef OSUETREE
	while (num_lines[0] > 0 || num_lines[1] > 0) {
		if (len1 < 0 || len2 < 0) {
			break;
		}
		if (num_lines[0] > 0 && (num_lines[1] <= 0 || strcmp(line1, line2) <= 0)) {
			puts(line1);
			--num_lines[0];
			len1 = getline_no_newline(&line1, &cap1, first_stream);
		} else {
			puts(line2);
			--num_lines[1];
			len2 = getline_no_newline(&line2, &cap2, second_stream);
		}
	}

	substep_length += strlen("forksort()");
	int between = 3;
	int indent = (len1 + len2 + between - substep_length + 1) / 2;
	int max_len2 = len2;
	for (int i = 0; i < indent; ++i) {
		putchar(' ');
	}
	printf("forksort(%s)", substep);
	for (int i = 0; i < indent; ++i) {
		putchar(' ');
	}
	putchar('\n');
	for (int i = 0; i < indent + 2; ++i) {
		putchar(' ');
	}
	putchar('/');
	for (int i = 0; i < substep_length - 6; ++i) {
		putchar(' ');
	}
	putchar('\\');
	for (int i = 0; i < indent + 2; ++i) {
		putchar(' ');
	}
	putchar('\n');
	while (len1 >= 0) {
		printf("%s", line1);
		for (int i = 0; i < between; ++i) {
			putchar(' ');
		}
		int align = max_len2;
		if (len2 > 0) {
			printf("%s", line2);
			align -= len2;
		}
		for (int i = 0; i < align; ++i) {
			putchar(' ');
		}
		putchar('\n');
		len1 = getline_no_newline(&line1, &cap1, first_stream);
		len2 = getline_no_newline(&line2, &cap2, second_stream);
	}
#else
	while (len1 >= 0 || len2 >= 0) {
		if (len1 >= 0 && (len2 < 0 || strcmp(line1, line2) <= 0)) {
			puts(line1);
			len1 = getline_no_newline(&line1, &cap1, first_stream);
		} else {
			puts(line2);
			len2 = getline_no_newline(&line2, &cap2, second_stream);
		}
	}
#endif // OSUETREE

	free(line1);
	free(line2);
	if (!feof(first_stream) || !feof(second_stream)) {
		return -1;
	}
	return 0;
}

/**
 * Peeks into the given stream for the next character. Returns EOF on end of
 * file or error.
 */
static int peek(FILE *stream)
{
	int c = getc(stream);
	if (c == EOF) {
		if (ferror(stream) != 0) {
			logger("getc");
		}
		return EOF;
	}
	ungetc(c, stdin);
	return c;
}

int main(int argc, char *argv[])
{
#ifdef OSUETREE
	argv0 = argc > 0 ? argv[0] : "bonus";
#else
	argv0 = argc > 0 ? argv[0] : "forksort";
#endif
	if (argc != 1) {
		fprintf(stderr, "Usage: %s\n", argv0);
		return EXIT_FAILURE;
	}

	int status = EXIT_SUCCESS;
	char *line = NULL;
	struct child first = {NULL, NULL};
	struct child second = {NULL, NULL};

	size_t cap = 0;
	ssize_t len = getline_no_newline(&line, &cap, stdin);
	if (len < 0) {
		if (feof(stdin)) {
			goto cleanup;
		}
		goto error;
	}
	if (peek(stdin) == EOF) {
		// There is only one line in stdin.
		puts(line);
#ifdef OSUETREE
		printf("forksort(%s)\n", line);
#endif
		goto cleanup;
	}

	if (fork_child(&first) < 0 || fork_child(&second) < 0) {
		goto error;
	}

	for (int pingpong = 0; len >= 0; pingpong = 1 - pingpong) {
#ifdef OSUETREE
		char const *delim = ",";
		char *ss = realloc(substep, substep_length + len + strlen(delim) + 1);
		if (ss == NULL) {
			logger("realloc");
			goto error;
		}
		substep = ss;
		if (substep_length > 0) {
			strcpy(substep + substep_length, delim);
			substep_length += strlen(delim);
		}
		strcpy(substep + substep_length, line);
		substep_length += len;
		++num_lines[pingpong];
#endif
		if (pingpong == 0) {
			fprintf(first.in, "%s\n", line);
		} else {
			fprintf(second.in, "%s\n", line);
		}
		len = getline_no_newline(&line, &cap, stdin);
	}
	if (!feof(stdin)) {
		goto error;
	}

	// Close child streams so they don't wait indefinitely for input.
	if (close_stream(&first.in) < 0 || close_stream(&second.in) < 0) {
		goto error;
	}

	if (merge(first.out, second.out) < 0) {
		goto error;
	}

cleanup:
	free(line);
#ifdef OSUETREE
	free(substep);
#endif
	close_stream(&first.in);
	close_stream(&first.out);
	close_stream(&second.in);
	close_stream(&second.out);
	// Wait until all child processes have terminated.
	do {
		int wstatus = 0;
		if (wait(&wstatus) > 0) {
			if (WEXITSTATUS(wstatus) != EXIT_SUCCESS) {
				logger("child process error");
				status = EXIT_FAILURE;
			}
			errno = 0;
		}
	} while (errno != ECHILD);
	return status;
error:
	status = EXIT_FAILURE;
	goto cleanup;
}
