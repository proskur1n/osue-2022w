/**
 * @author Andrey Proskurin 12122381
 * @date   2022-12-04
 * @brief  This program requests files from an http server.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#define CAPACITY 4096

/// Command-line arguments
typedef struct {
	char const *port;
	char const *outfile; // May be null
	char const *outdir; // May be null
	char *url;
} args_t;

/// Parsed url components
typedef struct {
	char const *port;
	char const *host;
	char const *path; // Does not begin with a slash (/) character.
	char *output; // Dynamically allocated string with the output filepath, or NULL for stdout.
} request_t;

/// Program exit code
typedef enum {
	SUCCESS = EXIT_SUCCESS,
	CLIENT_ERROR = EXIT_FAILURE,
	PROTOCOL_ERROR = 2,
	STATUS_ERROR = 3
} status_t;

static char const *argv0 = "unknown";

/// Prints a custom error message to stderr.
static void print_custom_error(char const *msg, char const *explanation)
{
	fprintf(stderr, "[%s] %s", argv0, msg);
	if (explanation != NULL) {
		fprintf(stderr, ": %s\n", explanation);
	} else {
		fprintf(stderr, "\n");
	}
}

/// Prints an error message followed by errno information to stderr.
static void print_error(char const *msg)
{
	print_custom_error(msg, strerror(errno));
}

/**
 * Parses command-line arguments from argc and argv. Returns 0 on success and -1 on failure.
 */
static int parse_argv(args_t *result, int argc, char *argv[])
{
	memset(result, 0, sizeof(*result));
	char *port = NULL;
	char *outfile = NULL;
	char *outdir = NULL;
	int opt;
	while ((opt = getopt(argc, argv, "p:o:d:")) != -1) {
		switch (opt) {
		case 'p':
			if (port != NULL || optarg[0] == '\0') {
				return -1;
			}
			port = optarg;
			break;
		case 'o':
			if (outfile != NULL || outdir != NULL || optarg[0] == '\0') {
				return -1;
			}
			outfile = optarg;
			break;
		case 'd':
			if (outfile != NULL || outdir != NULL || optarg[0] == '\0') {
				return -1;
			}
			outdir = optarg;
			break;
		default:
			return -1;
		}
	}

	if (optind + 1 != argc) {
		return -1;
	}
	*result = (args_t) {
		.port = port == NULL ? "80" : port,
		.outfile = outfile,
		.outdir = outdir,
		.url = argv[optind]
	};
	return 0;
}

/**
 * Parses the request information from the url specified in the command-line arguments. Returns 0 on
 * success and -1 on failure.
 */
static int get_request_info(request_t *r, int argc, char *argv[])
{
	args_t args = {0};
	if (parse_argv(&args, argc, argv) < 0) {
		fprintf(stderr, "Usage: %s [-p PORT] [-o FILE | -d DIR] URL\n", argv0);
		return -1;
	}

	char const *proto = "http://";
	if (strncmp(args.url, proto, strlen(proto)) != 0) {
		print_custom_error("url must begin with http://", NULL);
		return -1;
	}

	char *host = args.url + strlen(proto);
	char *path = strchr(host, '/');
	char *filename = "";
	if (path == NULL) {
		// No final slash
		path = "";
	} else {
		filename = strrchr(path, '/') + 1;
		++path; // Skip first slash
	}
	host[strcspn(host, ";/?:@=&")] = 0;

	size_t filename_len = strcspn(filename, ";/?:@=&");
	if (filename_len == 0) {
		filename = "index.html";
		filename_len = strlen(filename);
	}

	char *output = NULL;
	if (args.outfile != NULL) {
		output = strdup(args.outfile);
	} else if (args.outdir != NULL) {
		size_t dir_len = strlen(args.outdir);
		output = calloc(dir_len + filename_len + 2, sizeof(char));
		if (output != NULL) {
			strcpy(output, args.outdir);
			if (dir_len == 0 || args.outdir[dir_len - 1] != '/') {
				output[dir_len++] = '/';
			}
			memcpy(output + dir_len, filename, filename_len);
			output[dir_len + filename_len] = 0;
		}
	}
	if ((args.outfile != NULL || args.outdir != NULL) && output == NULL) {
		print_error("calloc");
		return -1;
	}

	*r = (request_t) {
		.port = args.port,
		.host = host,
		.path = path,
		.output = output
	};
	return 0;
}

/**
 * Returns an addrinfo struct pointer with the specified host and port number, or NULL on failure.
 *
 * @param host Requested hostname
 * @param port Requested port number
 */
static struct addrinfo *get_addrinfo_from_hostname(char const *host, char const *port)
{
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	struct addrinfo *ai = NULL;
	int status = getaddrinfo(host, port, &hints, &ai);
	if (status != 0) {
		print_custom_error("getaddrinfo", gai_strerror(status));
		if (ai != NULL) {
			freeaddrinfo(ai);
		}
		return NULL;
	}
	return ai;
}

/**
 * Creates a new socket from the given request and waits for a successful connection to the server.
 * Returns NULL on failure.
 *
 * @param request Request information
 */
static FILE *connect_to_server(request_t req)
{
	struct addrinfo *ai = get_addrinfo_from_hostname(req.host, req.port);
	if (ai == NULL) {
		return NULL;
	}

	int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (sock < 0) {
		print_error("socket");
		freeaddrinfo(ai);
		return NULL;
	}

	while (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
		if (errno != EINTR) {
			print_error("connect");
			close(sock);
			freeaddrinfo(ai);
			return NULL;
		}
	}

	FILE *f = fdopen(sock, "r+");
	if (f == NULL) {
		print_error("fdopen");
		close(sock);
	}
	freeaddrinfo(ai);
	return f;
}

/**
 * Writes an http request with the given parameters to the specified server connection. Returns 0 on
 * success and -1 on failure.
 */
static int send_http_request(FILE *connection, request_t req)
{
	int status = fprintf(connection,
		"GET /%s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Accept-Encoding: gzip\r\n"
		"User-Agent: osue-http-client/1.0\r\n" // Some websites respond "403 Forbidden" without a user-agent.
		"Connection: close\r\n\r\n", req.path, req.host);
	if (status < 0 || fflush(connection) == EOF) {
		print_error("fprintf");
		return -1;
	}
	return 0;
}

/**
 * Parses the first line of an incoming http response. Returns 0 on success and nonzero status code
 * on failure.
 */
static status_t parse_status_line(char *line)
{
	char const *version = "HTTP/1.1";
	if (strncmp(line, version, strlen(version)) != 0) {
		return PROTOCOL_ERROR;
	}
	line += strlen(version);
	while (isspace(*line)) {
		++line;
	}

	char *end = NULL;
	long status = strtol(line, &end, 10);
	if (end == line || !isspace(*end)) {
		return PROTOCOL_ERROR;
	}
	if (status != 200) {
		line[strcspn(line, "\r\n")] = 0;
		fprintf(stderr, "%s\n", line);
		return STATUS_ERROR;
	}

	return SUCCESS;
}

/**
 * Reads the body of an uncompressed response and writes its content to the output file.
 *
 * @param server Server from which to read the response body
 * @param output Destination for the body content
 */
static status_t read_body_uncompressed(FILE *server, FILE *output)
{
	unsigned char buf[CAPACITY];
	size_t len = 0;

	while ((len = fread(buf, sizeof(unsigned char), CAPACITY, server)) > 0) {
		if (fwrite(buf, sizeof(unsigned char), len, output) < len) {
			print_error("fwrite");
			return CLIENT_ERROR;
		}
	}

	if (ferror(server)) {
		print_error("fread");
		return PROTOCOL_ERROR;
	}
	return SUCCESS;
}

/**
 * Reads the body of a gzipped response and writes its content to the output file.
 *
 * @param server Server from which to read the response body
 * @param output Destination for the body content
 */
static status_t read_body_gzip(FILE *server, FILE *output)
{
	z_stream zstream = {0};
	int windowBits = MAX_WBITS + 16; // Adding 16 switches the decompression to gzip mode.
	if (inflateInit2(&zstream, windowBits) != Z_OK) {
		print_custom_error("inflateInit2", zstream.msg);
		return CLIENT_ERROR;
	}

	status_t status = SUCCESS;
	unsigned char gzipped[CAPACITY];
	unsigned char ungzipped[CAPACITY];
	int ret = Z_OK;

	while (ret != Z_STREAM_END) {
		zstream.next_in = gzipped;
		zstream.avail_in = fread(gzipped, sizeof(unsigned char), CAPACITY, server);
		if (ferror(server)) {
			print_error("fread(server)");
			status = PROTOCOL_ERROR;
			goto cleanup;
		}
		if (zstream.avail_in == 0) {
			goto cleanup;
		}

		do {
			zstream.avail_out = CAPACITY;
			zstream.next_out = ungzipped;
			ret = inflate(&zstream, Z_NO_FLUSH);
			if (ret != Z_OK && ret != Z_STREAM_END) {
				print_custom_error("inflate", zstream.msg);
				status = PROTOCOL_ERROR;
				goto cleanup;
			}
			size_t have = CAPACITY - zstream.avail_out;
			if (fwrite(ungzipped, sizeof(unsigned char), have, output) < have) {
				print_error("fwrite");
				status = CLIENT_ERROR;
				goto cleanup;
			}
		} while (zstream.avail_out == 0);
	}

cleanup:
	if (inflateEnd(&zstream) != Z_OK) {
		print_custom_error("inflateEnd", zstream.msg);
		status = CLIENT_ERROR;
	}
	return status;
}

/**
 * Reads the body of an http response and writes its content to the output file.
 *
 * @param server Server from which to read the response body
 * @param output Filepath where to save the content or NULL for stdout
 * @param gzip   Indicates whether to use gzip decompression
 */
static status_t read_body(FILE *server, char const *output, int gzip)
{
	FILE *file = stdout;
	if (output != NULL) {
		file = fopen(output, "w");
		if (file == NULL) {
			print_error("fopen");
			return CLIENT_ERROR;
		}
	}

	status_t status = SUCCESS;
	if (gzip) {
		status = read_body_gzip(server, file);
	} else {
		status = read_body_uncompressed(server, file);
	}
	if (file != stdout) {
		fclose(file);
	}
	return status;
}

/**
 * Requests the specified file from an http server and outputs its content to the given outputination.
 *
 * @param server Server connection from which to request the file
 * @param req    Request information
 */
static status_t request_file(FILE *server, request_t req)
{
	if (send_http_request(server, req) < 0) {
		return CLIENT_ERROR;
	}

	status_t status = PROTOCOL_ERROR;
	char *line = NULL;
	size_t cap = 0;
	int gzip = 0;
	int have_header_end = 0;

	if (getline(&line, &cap, server) > 0) {
		status = parse_status_line(line);
	}
	if (status != SUCCESS) {
		free(line);
		if (status == PROTOCOL_ERROR) {
			fputs("Protocol error!\n", stderr);
		}
		return status;
	}

	while (getline(&line, &cap, server) > 0) {
		if (strcmp(line, "\r\n") == 0) {
			have_header_end = 1;
			break;
		}
		char *field = strtok(line, " :");
		char *value = strtok(NULL, " :\t\r\n");
		if (field == NULL || value == NULL) {
			continue;
		}
		if (strcasecmp(field, "Content-Encoding") == 0 && strcmp(value, "gzip") == 0) {
			gzip = 1;
		}
	}

	free(line);
	if (have_header_end) {
		status = read_body(server, req.output, gzip);
	} else {
		print_error("getline");
		status = PROTOCOL_ERROR;
	}

	if (status == PROTOCOL_ERROR) {
		fputs("Protocol error!\n", stderr);
	}
	return status;
}

int main(int argc, char *argv[])
{
	argv0 = argc > 0 ? argv[0] : "client";
	status_t status = CLIENT_ERROR;
	request_t req = {0};
	FILE *connection = NULL;

	if (get_request_info(&req, argc, argv) == 0) {
		connection = connect_to_server(req);
		if (connection != NULL) {
			status = request_file(connection, req);
		}
	}

	if (connection != NULL) {
		fclose(connection);
	}
	free(req.output);
	return status;
}
