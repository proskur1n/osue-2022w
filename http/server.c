/**
 * @author Andrey Proskurin 12122381
 * @date   2022-12-05
 * @brief  This program serves files to http clients.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <zlib.h>

#define BACKLOG 8
#define CAPACITY 4096

/// Command-line arguments
typedef struct {
	char const *port;
	char const *index;
	char const *root;
} args_t;

/// Http status codes
typedef enum {
	OK = 200,
	BAD_REQUEST = 400,
	NOT_FOUND = 404,
	INTERNAL_SERVER_ERROR = 500,
	NOT_IMPLEMENTED = 501,
} http_status_t;

static char const *argv0 = "unknown";
static volatile sig_atomic_t quit = 0;
static z_stream zstream;

/// Prints a custom error message to the stderr.
static void print_custom_error(char const *msg, char const *explanation)
{
	fprintf(stderr, "[%s] %s", argv0, msg);
	if (explanation != NULL) {
		fprintf(stderr, ": %s\n", explanation);
	} else {
		fprintf(stderr, "\n");
	}
}

/// Prints an error message followed by errno information to the stderr.
static void print_error(char const *msg)
{
	print_custom_error(msg, strerror(errno));
}

/**
 * Parses command-line arguments from argc and argv and stores them in result. Returns 0 on success
 * and -1 on failure.
 */
static int parse_argv(args_t *result, int argc, char *argv[])
{
	memset(result, 0, sizeof(*result));
	char *port = NULL;
	char *index = NULL;
	int opt;
	while ((opt = getopt(argc, argv, "p:i:")) != -1) {
		switch (opt) {
		case 'p':
			if (port != NULL || optarg[0] == '\0') {
				return -1;
			}
			port = optarg;
			break;
		case 'i':
			if (index != NULL || optarg[0] == '\0') {
				return -1;
			}
			index = optarg;
			break;
		default:
			return -1;
		}
	}
	if (optind + 1 != argc) {
		return -1;
	}

	*result = (args_t) {
		.port = port == NULL ? "8080" : port,
		.index = index == NULL ? "index.html" : index,
		.root = argv[optind]
	};
	return 0;
}

/**
 * Returns a new passive socket with the specified port number, or -1 on failure.
 */
static int create_server_socket(char const *port)
{
	struct addrinfo *ai = NULL;
	int sockfd = -1;

	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE
	};
	int status = getaddrinfo(NULL, port, &hints, &ai);
	if (status != 0) {
		print_custom_error("getaddrinfo", gai_strerror(status));
		goto error;
	}

	sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sockfd < 0) {
		print_error("socket");
		goto error;
	}

	int optval = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		print_error("setsockopt");
		goto error;
	}

	if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		print_error("bind");
		goto error;
	}
	if (listen(sockfd, BACKLOG) < 0) {
		print_error("listen");
		goto error;
	}

	freeaddrinfo(ai);
	return sockfd;
error:
	if (sockfd >= 0) {
		close(sockfd);
	}
	if (ai != NULL) {
		freeaddrinfo(ai);
	}
	return -1;
}

/**
 * Parses the first line of an incoming http request. Returns an error code if parsing failed, or OK
 * if the request was valid.
 *
 * @param full_path [out] Expanded filepath for the requested url. Must be free'd by the caller.
 * @param line      First line of the request message.
 * @param args      Command-line arguments
 */
static http_status_t parse_first_request_line(char **full_path, char *line, args_t args)
{
	*full_path = NULL;
	char const *delim = " \t\r\n";
	char *method = strtok(line, delim);
	char *path = strtok(NULL, delim);
	char *version = strtok(NULL, delim);
	if (method == NULL || path == NULL || version == NULL || strtok(NULL, delim) != NULL) {
		return BAD_REQUEST;
	}
	if (strcmp(version, "HTTP/1.1") != 0 || path[0] == '\0') {
		return BAD_REQUEST;
	}
	if (strcmp(method, "GET") != 0) {
		return NOT_IMPLEMENTED;
	}

	size_t root_len = strlen(args.root);
	size_t path_len = strlen(path);
	char *fp = calloc(root_len + path_len + strlen(args.index) + 3, sizeof(char));
	if (fp == NULL) {
		return INTERNAL_SERVER_ERROR;
	}
	strcpy(fp, args.root);
	if (root_len == 0 || args.root[root_len - 1] != '/') {
		fp[root_len++] = '/';
	}
	strcpy(fp + root_len, path);
	if (path_len > 0 && path[path_len - 1] == '/') {
		strcpy(fp + root_len + path_len, args.index);
	}
	*full_path = fp;
	return OK;
}

/**
 * Returns a textual representation of the given http status code. The string is allocated in
 * static memory.
 */
static char const *status_to_string(http_status_t status)
{
	switch (status) {
	case OK:
		return "OK";
	case BAD_REQUEST:
		return "Bad Request";
	case NOT_FOUND:
		return "Not Found";
	case INTERNAL_SERVER_ERROR:
		return "Internal Server Error";
	case NOT_IMPLEMENTED:
		return "Not Implemented";
	}
	return "Unreachable Statement";
}

/**
 * Returns the file size in bytes, or -1 on failure.
 */
static long get_file_size(FILE *file)
{
	if (fseek(file, 0, SEEK_END) == -1) {
		return -1;
	}
	long sz = ftell(file);
	if (sz == -1) {
		return -1;
	}
	rewind(file);
	return sz;
}

/**
 * Returns the current date and time in the RFC 822 format. The result is stored in static memory.
 */
static char const *get_rfc822(void)
{
	enum { BUF_CAP = 64 };
	static char buf[BUF_CAP];
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);
	if (tm == NULL || strftime(buf, BUF_CAP, "%a, %d %b %y %T %Z", tm) == 0) {
		strncpy(buf, "Thu, 01 Jan 70 00:00:00 GMT", BUF_CAP);
		buf[BUF_CAP - 1] = 0;
	}
	return buf;
}

/**
 * Returns the MIME-type of the given filepath, or NULL if its type is unknown. The result is
 * stored in static memory.
 */
static char const *get_mime(char *full_path)
{
	char *ext = strrchr(full_path, '.');
	if (ext == NULL) {
		return NULL;
	}
	if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
		return "text/html";
	}
	if (strcmp(ext, ".css") == 0) {
		return "text/css";
	}
	if (strcmp(ext, ".js") == 0) {
		return "application/javascript";
	}
	return NULL;
}

/**
 * Reads the raw file content specified by path and stores a pointer to a dynamically allocated
 * buffer in 'content'. Returns 0 on success and -1 on failure.
 *
 * @param content   [out] Pointer to the file content
 * @param length    [out] Size of the allocated buffer
 * @param file      File for reading
 * @param file_size Number of bytes to read from file
 */
static int get_file_content_raw(unsigned char **content, size_t *length, FILE *file, size_t file_size)
{
	*content = NULL;
	*length = 0;
	unsigned char *buffer = malloc(file_size + 1); // Passing 0 to malloc is not portable.
	if (buffer == NULL) {
		print_error("malloc");
		return -1;
	}
	if (fread(buffer, sizeof(unsigned char), file_size, file) < file_size) {
		print_error("fread");
		free(buffer);
		return -1;
	}
	*content = buffer;
	*length = file_size;
	return 0;
}

/**
 * Deflates the file content specified by path and stores a pointer to a dynamically allocated
 * buffer in 'content'. Returns 0 on success and -1 on failure.
 *
 * @param content   [out] Pointer to the deflated file content
 * @param length    [out] Size of the allocated buffer
 * @param file      File for reading
 * @param file_size Number of bytes to read from file
 */
static int get_file_content_gzip(unsigned char **content, size_t *length, FILE *file, size_t file_size)
{
	*content = NULL;
	*length = 0;
	if (deflateReset(&zstream) != Z_OK) {
		print_custom_error("deflateReset", zstream.msg);
		return -1;
	}

	int zret = Z_OK;
	unsigned char temp[CAPACITY];
	uLong bound = deflateBound(&zstream, file_size);
	unsigned char *gzipped = malloc(bound + 1);
	if (gzipped == NULL) {
		print_error("malloc");
		return -1;
	}

	zstream.next_out = gzipped;
	zstream.avail_out = bound;
	while (zret == Z_OK && !feof(file)) {
		zstream.next_in = temp;
		zstream.avail_in = fread(temp, sizeof(unsigned char), CAPACITY, file);
		if (ferror(file)) {
			break;
		}
		zret = deflate(&zstream, Z_NO_FLUSH);
	}
	if (feof(file)) {
		zret = deflate(&zstream, Z_FINISH);
	}

	if (zret == Z_STREAM_END) {
		*content = gzipped;
		*length = bound - zstream.avail_out;
		return 0;
	}

	free(gzipped);
	if (ferror(file)) {
		print_error("ferror");
	} else {
		print_custom_error("deflate", zstream.msg);
	}
	return -1;
}

/**
 * Reads the contents of the file specified by path and stores a pointer to a dynamically allocated
 * buffer in 'content'. This procedure can optionally use a gzip compression when reading the data.
 *
 * @param content [out] Pointer to the file content
 * @param length  [out] Size of the allocated buffer
 * @param path    Filepath
 * @param gzip    Indicates whether to use gzip compression
 */
static http_status_t get_file_content(unsigned char **content, size_t *length, char const *path, int gzip)
{
	*content = NULL;
	*length = 0;

	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return NOT_FOUND;
	}

	long file_size = get_file_size(file);
	if (file_size < 0) {
		fclose(file);
		return NOT_FOUND;
	}

	int ret = 0;
	if (gzip) {
		ret = get_file_content_gzip(content, length, file, file_size);
	} else {
		ret = get_file_content_raw(content, length, file, file_size);
	}
	fclose(file);
	if (ret != 0) {
		return INTERNAL_SERVER_ERROR;
	}
	return OK;
}

/**
 * Reads the request from an http client and sends a response. The client connection is NOT
 * automatically closed afterwards.
 */
static http_status_t respond_to_request(FILE *client, args_t args)
{
	http_status_t status = BAD_REQUEST;
	char *line = NULL;
	char *full_path = NULL;
	unsigned char *content = NULL;
	size_t content_length = 0;
	int use_gzip = 0;
	size_t cap = 0;

	if (getline(&line, &cap, client) > 0) {
		status = parse_first_request_line(&full_path, line, args);
	}
	while (getline(&line, &cap, client) > 0 && strcmp(line, "\r\n") != 0) {
		char *field = strtok(line, " :");
		char *value = strtok(NULL, "");
		if (field == NULL || value == NULL) {
			continue;
		}
		if (strcasecmp(field, "Accept-Encoding") == 0 && strstr(value, "gzip") != NULL) {
			use_gzip = 1;
		}
	}
	if (ferror(client) || feof(client)) {
		status = BAD_REQUEST;
	}

	if (status == OK) {
		status = get_file_content(&content, &content_length, full_path, use_gzip);
	}

	fprintf(client, "HTTP/1.1 %d %s\r\n", status, status_to_string(status));
	fprintf(client, "Date: %s\r\n", get_rfc822());
	fputs("Connection: close\r\n", client);
	if (status == OK) {
		fprintf(client, "Content-Length: %ld\r\n", content_length);
		if (use_gzip) {
			fputs("Content-Encoding: gzip\r\n", client);
		}
		char const *mime = get_mime(full_path);
		if (mime != NULL) {
			fprintf(client, "Content-Type: %s\r\n", mime);
		}
	}
	fputs("\r\n", client);

	if (status == OK) {
		// Write the response body.
		fwrite(content, sizeof(unsigned char), content_length, client);
	}

	free(full_path);
	free(line);
	free(content);
	return status;
}

/// sigaction(3) callback
static void signal_handler(int sig)
{
	(void) sig;
	quit = 1;
}

/**
 * Sets the signal handlers for SIGINT and SIGTERM with the specified sa_flags.
 */
static void set_signal_handler(int flags)
{
	struct sigaction sa = {
		.sa_handler = signal_handler,
		.sa_flags = flags
	};
	if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
		print_error("sigaction");
	}
}

int main(int argc, char *argv[])
{
	argv0 = argc > 0 ? argv[0] : "server";
	args_t args = {0};
	if (parse_argv(&args, argc, argv) < 0) {
		fprintf(stderr, "Usage: %s [-p PORT] [-i INDEX] DOC_ROOT\n", argv0);
		return EXIT_FAILURE;
	}

	int windowBits = MAX_WBITS + 16; // Adding 16 switches the compression to gzip mode.
	int memLevel = 8;
	if (deflateInit2(&zstream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, memLevel, Z_DEFAULT_STRATEGY) != Z_OK) {
		print_custom_error("deflateInit2", zstream.msg);
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	int main_socket = create_server_socket(args.port);
	if (main_socket < 0) {
		goto error;
	}

	while (!quit) {
		set_signal_handler(0);

		int fd = accept(main_socket, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR) {
				continue;
			}
			print_error("accept");
			goto error;
		}

		set_signal_handler(SA_RESTART);

		FILE *client = fdopen(fd, "r+");
		if (client == NULL) {
			print_error("fdopen(client)");
			close(fd);
			goto error;
		}

		http_status_t status = respond_to_request(client, args);
		fclose(client);
		if (status == INTERNAL_SERVER_ERROR) {
			goto error;
		}
	}

cleanup:
	if (deflateEnd(&zstream) == Z_STREAM_ERROR) {
		print_custom_error("deflateEnd", zstream.msg);
		ret = EXIT_FAILURE;
	}
	if (main_socket >= 0 && close(main_socket) < 0) {
		print_error("close(main_socket)");
		ret = EXIT_FAILURE;
	}
	return ret;
error:
	ret = EXIT_FAILURE;
	goto cleanup;
}
