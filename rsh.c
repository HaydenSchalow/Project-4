#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>

#define N 13

extern char **environ;
char uName[20];

char *allowed[N] = {"cp","touch","mkdir","ls","pwd","cat","grep","chmod","diff","cd","exit","help","sendmsg"};

struct message {
	char source[50];
	char target[50];
	char msg[200];
};

void terminate(int sig) {
    printf("Exiting....\n");
    fflush(stdout);
    exit(0);
}

void sendmsg (char *user, char *target, char *msg) {
	// prepare message struct and write to serverFIFO
	struct message req;
	memset(&req, 0, sizeof(req));
	strncpy(req.source, user, sizeof(req.source)-1);
	strncpy(req.target, target, sizeof(req.target)-1);
	strncpy(req.msg, msg, sizeof(req.msg)-1);

	int sfd = open("serverFIFO", O_WRONLY);
	if (sfd == -1) {
		perror("open serverFIFO for writing");
		return;
	}

	ssize_t w = write(sfd, &req, sizeof(req));
	if (w != sizeof(req)) {
		// write may be partial or error
		perror("write to serverFIFO");
	}
	close(sfd);
}

void* messageListener(void *arg) {
	// Listen to user's FIFO for incoming messages.
	struct message incoming;
	int fd;
	while (1) {
		// Open user's FIFO for reading. If it doesn't exist or fails, wait and retry.
		fd = open(uName, O_RDONLY);
		if (fd == -1) {
			// couldn't open, wait a bit and retry
			sleep(1);
			continue;
		}

		// Read messages repeatedly from the FIFO until it is closed on the write side
		ssize_t r;
		while ((r = read(fd, &incoming, sizeof(incoming))) > 0) {
			// print the incoming message in requested format
			// Note: incoming.msg is expected to be null-terminated
			printf("\nIncoming message from %s: %s\n", incoming.source, incoming.msg);
			fflush(stdout);
			// re-print prompt if user is at prompt (not strictly necessary)
			fprintf(stderr,"rsh>");
			fflush(stderr);
		}

		// if read returned 0 or -1, close and reopen to continue listening
		close(fd);
		// small sleep to avoid busy loop
		usleep(100000);
	}

	pthread_exit((void*)0);
}

int isAllowed(const char*cmd) {
	int i;
	for (i=0;i<N;i++) {
		if (strcmp(cmd,allowed[i])==0) {
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
    pid_t pid;
    char **cargv;
    char *path;
    char line[256];
    int status;
    posix_spawnattr_t attr;
    pthread_t tid;

    if (argc!=2) {
		printf("Usage: ./rsh <username>\n");
		exit(1);
    }
    signal(SIGINT,terminate);

    strcpy(uName,argv[1]);

    // create the message listener thread
    if (pthread_create(&tid, NULL, messageListener, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }
    pthread_detach(tid);

    while (1) {

		fprintf(stderr,"rsh>");

		if (fgets(line,256,stdin)==NULL) continue;

		if (strcmp(line,"\n")==0) continue;

		line[strlen(line)-1]='\0';

		char cmd[256];
		char line2[256];
		strcpy(line2,line);
		strcpy(cmd,strtok(line," "));

		if (!isAllowed(cmd)) {
			printf("NOT ALLOWED!\n");
			continue;
		}

		if (strcmp(cmd,"sendmsg")==0) {
			// parse target and message from line2
			// line2: "sendmsg target message..."
			char *first_space = strchr(line2, ' ');
			if (first_space == NULL) {
				printf("sendmsg: you have to specify target user\n");
				continue;
			}
			char *after_cmd = first_space + 1;
			// find next space separating target and message
			char *second_space = strchr(after_cmd, ' ');
			if (second_space == NULL) {
				// no second space -> no message provided
				// but might be that user typed only target (no message)
				// check if there is a target token
				if (strlen(after_cmd) == 0) {
					printf("sendmsg: you have to specify target user\n");
				} else {
					printf("sendmsg: you have to enter a message\n");
				}
				continue;
			}
			// extract target
			size_t tlen = second_space - after_cmd;
			if (tlen == 0) {
				printf("sendmsg: you have to specify target user\n");
				continue;
			}
			char target[50];
			memset(target, 0, sizeof(target));
			if (tlen >= sizeof(target)) tlen = sizeof(target)-1;
			strncpy(target, after_cmd, tlen);
			// message is the rest after second_space + 1
			char *msg = second_space + 1;
			if (msg == NULL || strlen(msg) == 0) {
				printf("sendmsg: you have to enter a message\n");
				continue;
			}
			// call sendmsg function
			sendmsg(uName, target, msg);
			continue;
		}

		if (strcmp(cmd,"exit")==0) break;

		if (strcmp(cmd,"cd")==0) {
			char *targetDir=strtok(NULL," ");
			if (strtok(NULL," ")!=NULL) {
				printf("-rsh: cd: too many arguments\n");
			}
			else {
				chdir(targetDir);
			}
			continue;
		}

		if (strcmp(cmd,"help")==0) {
			printf("The allowed commands are:\n");
			for (int i=0;i<N;i++) {
				printf("%d: %s\n",i+1,allowed[i]);
			}
			continue;
		}

		cargv = (char**)malloc(sizeof(char*));
		cargv[0] = (char *)malloc(strlen(cmd)+1);
		path = (char *)malloc(9+strlen(cmd)+1);
		strcpy(path,cmd);
		strcpy(cargv[0],cmd);

		char *attrToken = strtok(line2," "); /* skip cargv[0] which is completed already */
		attrToken = strtok(NULL, " ");
		int n = 1;
		while (attrToken!=NULL) {
			n++;
			cargv = (char**)realloc(cargv,sizeof(char*)*n);
			cargv[n-1] = (char *)malloc(strlen(attrToken)+1);
			strcpy(cargv[n-1],attrToken);
			attrToken = strtok(NULL, " ");
		}
		cargv = (char**)realloc(cargv,sizeof(char*)*(n+1));
		cargv[n] = NULL;

		// Initialize spawn attributes
		posix_spawnattr_init(&attr);

		// Spawn a new process
		if (posix_spawnp(&pid, path, NULL, &attr, cargv, environ) != 0) {
			perror("spawn failed");
			exit(EXIT_FAILURE);
		}

		// Wait for the spawned process to terminate
		if (waitpid(pid, &status, 0) == -1) {
			perror("waitpid failed");
			exit(EXIT_FAILURE);
		}

		// Destroy spawn attributes
		posix_spawnattr_destroy(&attr);

    }
    return 0;
}
