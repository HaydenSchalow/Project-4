#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

struct message {
	char source[50];
	char target[50];
	char msg[200]; // message body
};

void terminate(int sig) {
	printf("Exiting....\n");
	fflush(stdout);
	exit(0);
}

int main() {
	int server;
	int targetfd;
	int dummyfd;
	struct message req;
	signal(SIGPIPE,SIG_IGN);
	signal(SIGINT,terminate);

	// open serverFIFO for reading, and a dummy write end to avoid EOF
	server = open("serverFIFO",O_RDONLY);
	if (server == -1) {
		perror("open serverFIFO O_RDONLY");
		return 1;
	}
	dummyfd = open("serverFIFO",O_WRONLY);
	if (dummyfd == -1) {
		// not fatal, but print
		perror("open serverFIFO O_WRONLY (dummy)");
		// continue anyway
	}

	while (1) {
		// read requests from serverFIFO
		ssize_t r = read(server, &req, sizeof(req));
		if (r == 0) {
			// No writers; close and reopen read end to continue
			close(server);
			server = open("serverFIFO", O_RDONLY);
			if (server == -1) {
				perror("re-open serverFIFO O_RDONLY");
				sleep(1);
				continue;
			}
			continue;
		} else if (r < 0) {
			// read error
			if (errno == EINTR) continue;
			perror("read from serverFIFO");
			sleep(1);
			continue;
		} else if (r != sizeof(req)) {
			// partial read - ignore
			continue;
		}

		printf("Received a request from %s to send the message %s to %s.\n",req.source,req.msg,req.target);
		fflush(stdout);

		// open target FIFO and write the whole message struct to the target FIFO
		targetfd = open(req.target, O_WRONLY);
		if (targetfd == -1) {
			// couldn't open target FIFO; print error and continue
			fprintf(stderr, "Could not open target FIFO %s for writing: %s\n", req.target, strerror(errno));
			fflush(stderr);
			continue;
		}

		ssize_t w = write(targetfd, &req, sizeof(req));
		if (w != sizeof(req)) {
			perror("write to target FIFO");
		}

		close(targetfd);
	}
	close(server);
	if (dummyfd != -1) close(dummyfd);
	return 0;
}
