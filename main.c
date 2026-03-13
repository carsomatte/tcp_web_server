#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

# define LISTEN_BACKLOG 50
# define BUFFER_SIZE 1024
# define DOC_ROOT "./www"

volatile sig_atomic_t stop = 0;

void handle_sigint(int sig) {
	stop = 1;
}

int parse_request_line(int client_fd, char *method, size_t mlen, char *path, size_t plen, char *version, size_t vlen) {
	char buffer[BUFFER_SIZE] = {0};
	ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    	if (n <= 0) return -1;  // error or closed connection
	
	//printf("%s\n", buffer);

	buffer[n] = '\0';

	// Find the first line
    	char *line_end = strstr(buffer, "\r\n");
    	if (!line_end) return -1;  // malformed request
    	*line_end = '\0';

	// Currently am not using the sizes given as arguments. Will rewrite to allow for this
    	// Parse method, path, version
    	if (sscanf(buffer, "%7s %1023s %15s", method, path, version) != 3)
        	return -1;  // invalid request line

   	return 0;
}

const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";  // no extension

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".htm")  == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif")  == 0) return "image/gif";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    if (strcmp(ext, ".pdf")  == 0) return "application/pdf";

    return "application/octet-stream"; // default binary
}

void close_socket_helper(int fd) {
	if(close(fd) == -1) {
		perror("Closing Socket Failed");
		exit(EXIT_FAILURE);
	}
}

/*
 *****************
 RESPONSE HELPERS
 *****************
 */

void build_403_header(const char *version, char *response, size_t max_size) {
	snprintf(response, max_size, 
		"%s 403 Forbidden\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 14\r\n"
		"\r\n"
		"403 Forbidden\n",
		version);
}
void build_505_header(char *response, size_t max_size) {
	snprintf(response, max_size, 
		"HTTP/1.1 505 HTTP Version Not Supported\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 31\r\n"
		"\r\n"
		"505 HTTP Version Not Supported\n");
}
void build_405_header(const char *version, char *response, size_t max_size) {
	snprintf(response, max_size, 
		"%s 405 Method Not Allowed\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 23\r\n"
		"\r\n"
		"405 Method Not Allowed\n",
		version);
}
void build_404_header(const char *version, char *response, size_t max_size) {
	snprintf(response, max_size,
		"%s 404 Not Found\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 14\r\n"
		"\r\n"
		"404 Not Found\n",
		version);
}
void build_200_header(const char *version, char *response, size_t max_size, long int con_len, const char *con_type) {
	snprintf(response, max_size,
		"%s 200 OK\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %li\r\n"
		"\r\n", 
		version, con_type, con_len);
}
void build_400_header(char *response, size_t max_size) {
	snprintf(response, max_size,
		"HTTP/1.1 400 Bad Request\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 16\r\n"
		"\r\n"
		"400 Bad Request\n");
}


/*
 *****************
 SEND/BUILD HELPERS
 *****************
 */
void send_header(int client_fd, char *text) {
	size_t len = strlen(text);
	//printf("Header Response: \n%s\n", text);
	send(client_fd, text, len, 0);
	printf("response sent\n\n");
}

void send_body(int client_fd, const void *data, size_t len) {
	size_t total = 0;
	const char *p = data;

	while (total < len) {
		ssize_t n = send(client_fd, p+total, len-total, 0);
		if (n <= 0) return;
		total += n;
	}
}

void build_http_response(int client_fd, const char *method, const char *path, const char *version, char *response, size_t max_size) {
	// Unsupported HTTP Version. use 505
	if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) { // look into using strncmp		
		//printf("entered into unsupported http version block\n");
		build_505_header(response, max_size);
		send_header(client_fd, response);
		return;
	}
	// Unsupported Method. use 405
	if (strcmp(method, "GET") != 0) {
		build_405_header(version, response, max_size);
		send_header(client_fd, response);
		return;
	}
	
	// Create releative path for file
	char full_path[512];
	snprintf(full_path, sizeof(full_path), "%s%s", DOC_ROOT, path);
	printf("Relative Path: %s\n", full_path); // ex: ./www/some_dir_name
	

	struct stat st;
	if (stat(full_path, &st) < 0) { // path doesn't exist. use 404
		build_404_header(version, response, max_size);
		send_header(client_fd, response);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1); // append index.html to path
		if (stat(full_path, &st) < 0) { // directory exists but no index.html found. use 403
			build_403_header(version, response, max_size);
			send_header(client_fd, response);
			return;
		}
	}
	/* we know at this point the user requested a directory and index.html exists in it
	or the user requested a specific file in any directory and it exists */
	if (S_ISREG(st.st_mode)) { //regular file
		FILE *fp = fopen(full_path, "rb");
		if (!fp) {
			printf("Error Opening File\n");
			build_403_header(version, response, max_size);
			send_header(client_fd, response);
			return;
		}
		// Get file size for Content-Length header
		stat(full_path, &st);
		size_t file_size = st.st_size;
		// Get file type for Content-Type header
		const char *mime = get_mime_type(full_path);
		
		// Send Header
		build_200_header(version, response, max_size, file_size, mime);
		send_header(client_fd, response);

		// Send Body
		char body_buf[BUFFER_SIZE*4];
		size_t n;

		while ((n = fread(body_buf, 1, sizeof(body_buf), fp)) > 0) {
			send_body(client_fd, body_buf, n);
		}
	}
}

void *handle_client(void *arg) {
	int cfd = *(int *)arg;
	free(arg); // free memory in main thread

	char *header_response = calloc(BUFFER_SIZE * 2, sizeof(char)); // buffer for header only
	if (!header_response) {
		perror("calloc failure");
		close_socket_helper(cfd);
		return NULL;
	}
	size_t header_response_size_max = BUFFER_SIZE * 2;
	
	// Parse Data
	char method[8], path[1024], version[16];
	if (parse_request_line(cfd, method, sizeof(method), path, sizeof(path), version, sizeof(version)) == 0) {
		printf("Method:  %s\n", method);
		printf("Path:    %s\n", path);
		printf("Version: %s\n", version);
		
		// Build Response
		build_http_response(cfd, method, path, version, header_response, header_response_size_max);
	}	
	else {
		printf("Failed to parse request. Sending 400\n");
		build_400_header(header_response, header_response_size_max);
		send_header(cfd, header_response);
	}

	free(header_response);
	close_socket_helper(cfd);
	return NULL;
}

int main(int argc, char* argv[]) {
	int port;
	int server_fd, cfd;	
	int opt = 1; //for SO_REUSEADDR in setsockopt
	struct sockaddr_in server_addr, peer_addr;
	
	if (argc != 2) {
		printf("Usage: ./server <port>");
		return 1;
	}
	port = atoi(argv[1]);

	// Setup Signal Handler for graceful exit when pressing Ctrl+C
	struct sigaction sa = {0};
	sa.sa_handler = handle_sigint;
	sigaction(SIGINT, &sa, NULL);


	// Create Socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket Failed");
		exit(EXIT_FAILURE);
	}
	// Set Socket Options. Understand what SO_REUSEADDR does
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("Setting Socket Options Failed");
		exit(EXIT_FAILURE);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
	server_addr.sin_port = htons(port); // Convert port to big-endian 

	// Bind Socket
	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
		perror("Bind Failed");
		exit(EXIT_FAILURE);
	}
	// Listen for Incoming Connections
	if (listen(server_fd, LISTEN_BACKLOG) == -1) {
		perror("Listen Failed");
		exit(EXIT_FAILURE);
	}
	printf("Listening on port %d...\n", port);

	while (!stop) {
		// Accept incoming connections
		socklen_t peer_addr_size = sizeof(peer_addr);
		cfd = accept(server_fd, (struct sockaddr *)&peer_addr, &peer_addr_size);
		if (cfd == -1) {
			if (errno == EINTR && stop) { // Ctrl+C triggered shutdown
				break;
			}
			perror("Error Accepting Incoming Connection");
			continue;
		}
	
		pthread_t tid;

		int *pcfd = malloc(sizeof(int));
		*pcfd = cfd;

		if (pthread_create(&tid, NULL, handle_client, pcfd) != 0) {
			perror("p_thread Create Failure");
			close_socket_helper(cfd);
			free(pcfd);
			continue;
		}
		pthread_detach(tid);
	}
	
	close_socket_helper(server_fd);
	printf("Closed Sockets\n");
	return 0;
}
