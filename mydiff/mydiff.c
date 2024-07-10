/**
 * @author Andrey Proskurin 12122381
 * @date   2022-10-14
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

char const *argv0;

/**
 * Forwards filepath and mode to fopen(3) and prints an error message if the
 * the file could not be opened.
 *
 * @param filepath Same as in fopen(3).
 * @param mode     Ditto
 * @return FILE* or NULL on failure
 */
FILE *fopen_info(char const *filepath, char const *mode)
{
	FILE *f = fopen(filepath, mode);
	if (f == NULL) {
		fprintf(stderr, "%s: could not open file: %s\n", argv0, filepath);
	}
	return f;
}

/**
 * Wrapper around getline(3) that does not include the line delimiter into
 * the total line length. Same arguments as in getline(3).
 */
ssize_t getline_no_newline(char **lineptr, size_t *n, FILE *stream)
{
	ssize_t length = getline(lineptr, n, stream);
	if (length > 0 && (*lineptr)[length - 1] == '\n') {
		--length;
		(*lineptr)[length] = '\0';
	}
	return length;
}

int main(int argc, char *argv[])
{
	argv0 = argc > 0 ? argv[0] : "mydiff";
	char const *outfile_name = NULL;
	int insensitive = 0;

	int opt;
	while ((opt = getopt(argc, argv, "io:")) != -1) {
		switch (opt) {
		case 'i':
			insensitive = 1;
			break;
		case 'o':
			outfile_name = optarg;
			break;
		default:
			goto usage;
		}
	}
	if (optind + 2 != argc) {
		goto usage;
	}

	int status = EXIT_SUCCESS;
	FILE *outfile = outfile_name ? fopen_info(outfile_name, "w") : stdout;
	FILE *file1 = fopen_info(argv[optind], "r");
	FILE *file2 = fopen_info(argv[optind + 1], "r");
	if (outfile == NULL || file1 == NULL || file2 == NULL) {
		status = EXIT_FAILURE;
		goto cleanup;
	}

	size_t cap1 = 0;
	size_t cap2 = 0;
	char *line1 = NULL;
	char *line2 = NULL;

	for (size_t lineno = 1;; ++lineno) {
		ssize_t len1 = getline_no_newline(&line1, &cap1, file1);
		ssize_t len2 = getline_no_newline(&line2, &cap2, file2);
		if (len1 < 0 || len2 < 0) {
			break;
		}

		size_t min_len = (size_t) (len1 < len2 ? len1 : len2);
		if (insensitive) {
			for (size_t i = 0; i < min_len; ++i) {
				line1[i] = tolower(line1[i]);
				line2[i] = tolower(line2[i]);
			}
		}

		if (strncmp(line1, line2, min_len) != 0) {
			size_t mismatch = 0;
			for (size_t i = 0; i < min_len; ++i) {
				if (line1[i] != line2[i]) {
					++mismatch;
				}
			}
			fprintf(outfile, "Line: %ld, characters: %ld\n", lineno, mismatch);
		}
	}

	free(line1);
	free(line2);
	if (ferror(file1) != 0 || ferror(file2) != 0) {
		fprintf(stderr, "%s: error while reading the input files\n", argv0);
		status = EXIT_FAILURE;
		goto cleanup;
	}

cleanup:
	if (outfile && outfile != stdout) {
		fclose(outfile);
	}
	if (file1) {
		fclose(file1);
	}
	if (file2) {
		fclose(file2);
	}
	return status;
usage:
	fprintf(stderr, "Usage: %s [-i] [-o outfile] file1 file2\n", argv0);
	return EXIT_FAILURE;
}
