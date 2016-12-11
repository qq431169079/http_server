/*
 * Copyright (c) 2016, Tom G., <roboter972@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Created:		08.06.2016
 * Last modified:	13.12.2016
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "../include/http_server.h"

enum {
        OK = 0,                 /* HTTP 200 */
        NOT_FOUND,              /* HTTP 404 */
        NOT_IMPL,               /* HTTP 501 */
        NOT_SUPP,               /* HTTP 505 */
};

enum {
	VERLEN =	8,
	VERSZ =		9,
	MTHDSZ =	8,
        PATHSZ =        128,
	HDRSZ =		256,
	CHUNKSZ =	1024,
	REQSZ =		1040,
	RECSZ =		1032,
	BODYSZ =	1032
};

static struct server {
        struct sockaddr_in saddr;
        char request[REQSZ];
        char hdr[HDRSZ];
        char body[BODYSZ];
        char method[MTHDSZ];
        char path[PATHSZ];
        char httpver[VERSZ];
        char *ftype;
        int connfd;
        int newfd;
	int fd;
        off_t filesize;
} s;

/*
 * get_request(): Receive a request from client, through socket "newfd".
 *                Upon failure, terminate the calling process.
 */
static void get_request(void)
{
        if (recv(s.newfd, s.request, RECSZ, 0) < 0) {
                close(s.newfd);
            	server_error("recv()");
                exit(EXIT_FAILURE);
        }
}

/*
 * send_response(): Send a nul-terminated string to a client,
 *                  through socket "newfd".
 *                  Upon failure, terminate the calling process.
 */
static void send_response(const char *response)
{
        if (send(s.newfd, response, strlen(response) + 1, 0) < 0) {
                close(s.newfd);
                server_error("send()");
                exit(EXIT_FAILURE);
        }

}

/*
 * tokenize_request(): Validate request and tokenize first line of the http request.
 *                     If the request is invalid, send response to client and terminate calling process.
 */
static void tokenize_request(char *request)
{
        char *ptr;
        size_t occ, len;

        if (*request == ' ')
                goto out;

        for (occ = 0, ptr = request; *ptr && *ptr != '\n'; ) {
                if (*ptr++ == ' ') {
                        if (*ptr == ' ')
                                goto out;
                        occ++;
                }
        }

        if (occ != 2)
                goto out;

        ptr = strtok(request, " ");
        if (!ptr)
                return;

        if ((len = strlen(ptr)) >= MTHDSZ)
                len = MTHDSZ - 1;

        memcpy(s.method, ptr, len);

        ptr = strtok(NULL, " ");
        if (!ptr)
                return;

        /* Advance ptr so *ptr points to the first char of the requested file */
        if (*ptr == '/')
                ptr++;

        if ((len = strlen(ptr)) >= PATHSZ)
                len = PATHSZ - 1;

        memcpy(s.path, ptr, len);

        ptr = strtok(NULL, " ");
        if (!ptr)
                return;

        if ((len = strlen(ptr)) >= VERSZ)
                len = VERSZ - 1;

        memcpy(s.httpver, ptr, len);

        return;
out:
        send_response("Invalid http request; expected: GET<space><file><space><httpver> ...\n");
        close(s.newfd);
        exit(EXIT_FAILURE);
}

/*
 * get_http_status(): Return status string matching the http status number.
 */
static char *get_http_status(int nr)
{
        switch (nr) {
        case OK:
                return "HTTP/1.0 200 OK";
        case NOT_FOUND:
                return "HTTP/1.0 404 Not Found:";
        case NOT_IMPL:
                return "HTTP/1.0 501 Not Implemented:";
	case NOT_SUPP:
	        return "HTTP/1.0 505 Not Supported:";
        default:
                return "";
        }
}

/*
 * get_httperr_html(): Return html error string matching the http status number.
 */
static char *get_httperr_html(int nr)
{
        switch (nr) {
        case NOT_FOUND:
                return "<html><body><b>404</b>Page Not Found</body></html>";
        case NOT_IMPL:
                return "<html><body><b>501</b>Operation not supported</body></html>";
	case NOT_SUPP:
	        return "<html><body><b>505</b>Version Not Supported</body></html>";
        default:
                return "";
        }
}

/*
 * get_filetype(): Return type of the requested file.
 */
static char *get_filetype(const char *request)
{
	char *ptr;
        size_t len;

        if (((len = strlen(request)) + 6) < 15)
	       s.ftype = calloc(15, sizeof(char));
        else
                s.ftype = calloc(len + 6, sizeof(char));
	if (!s.ftype)
		return strncat(s.ftype, "unknown", 9);

	memcpy(s.ftype, "text/", 6);

        /* Get ptr to the file extension */
        if ((ptr = memchr(request, '.', strlen(request)))) {
                /* Advance ptr to the first extension char */
		ptr++;
                return strncat(s.ftype, ptr, strlen(ptr) + 6);
	}

        return strncat(s.ftype, "unknown", 9);
}

/*
 * gen_errorheader(): Copy all relevant data into hdr, so its ready for transmitting to client.
 */
static void gen_errorheader(int errnr, char *err)
{
        snprintf(s.hdr, HDRSZ, "%s %s\r\nContent-type: %s\r\n%s\r\n\r\n",
		get_http_status(errnr), err, get_filetype(s.path), get_httperr_html(errnr));
        free(s.ftype);
}

/*
 * gen_successheader(): Copy all relevant data into hdr, so its ready for transmitting to client.
 */
static void gen_successheader(void)
{
        snprintf(s.hdr, HDRSZ, "%s\r\nContent-type: %s\r\nContent-length: %ld\r\n\r\n",
				get_http_status(OK), get_filetype(s.path), s.filesize);
        free(s.ftype);
}

/*
 * validate_request(): Validate received request, and set filesize for hdr.
 *                     Upon failure, terminate the calling process.
 */
static void validate_request(void)
{
	struct stat st;

	if (memcmp(s.method, SUPP_MTHD, strlen(SUPP_MTHD))) {
		gen_errorheader(NOT_IMPL, s.method);
		goto out;
	}

        s.fd = open(s.path, O_RDONLY);
	if (s.fd < 0) {
		gen_errorheader(NOT_FOUND, s.path);
		goto out;
	}

	if (memcmp(s.httpver, HTTP10, strlen(s.httpver)) && memcmp(s.httpver, HTTP11, strlen(s.httpver))) {
		gen_errorheader(NOT_SUPP, s.httpver);
		goto out2;
	}

	if (fstat(s.fd, &st)) {
		gen_errorheader(NOT_FOUND, s.path);
		goto out2;
	}
	s.filesize = st.st_size;

	return;
out2:
        close(s.fd);
out:
	send_response(s.hdr);
	close(s.newfd);
	server_out(OUT_DBG, "Sent:\n%s\n", s.hdr);
	exit(EXIT_FAILURE);
}

/*
 * sockclose(): Upon receiving SIGINT, close open sockets and terminate the calling process.
 */
void sockclose(__attribute__((unused)) int sig)
{
	server_out(OUT_DBG, "\nSIGINT received, terminating process %d\n", getpid());
        close(s.connfd);

        exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	socklen_t len = sizeof(struct sockaddr_in);
	pid_t pid;
	int hbo;

	if (argc != 2) {
		server_error("Usage: ./server portnumber");
                return EXIT_FAILURE;
        }

        /* Create IPv4 TCP Socket */
	if ((s.connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		server_error("socket()");
		return EXIT_FAILURE;
	}

        signal(SIGINT, sockclose);

	hbo = (int) strtol(argv[1], (char **) NULL, 10);
	if (hbo <= LOWER_PORT_LIM || hbo >= UPPER_PORT_LIM) {
		server_error("strtol()");
                close(s.connfd);
		return EXIT_FAILURE;
	}

	s.saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	s.saddr.sin_family = AF_INET;
	s.saddr.sin_port = htons((unsigned short) hbo);

        /* Bind adress to socket, so a client is able to connect */
	if (bind(s.connfd, (struct sockaddr *) &s.saddr, len)) {
		server_error("bind()");
                close(s.connfd);
		return EXIT_FAILURE;
	}

	if (listen(s.connfd, SOMAXCONN)) {
		server_error("listen()");
		close(s.connfd);
                return EXIT_FAILURE;
	}

	server_out(OUT_DBG, "Listening ...\n");

        /* Prevent childs from going into "zombie" state */
        signal(SIGCHLD, SIG_IGN);

	while (1) {
                /* Parent process handles connections */
                s.newfd = accept(s.connfd, (struct sockaddr *) &s.saddr, &len);
                if (s.newfd < 0) {
                        server_error("accept()");
                        close(s.connfd);
                        return EXIT_FAILURE;
                }

                server_out(OUT_DBG, "Accepted connection ...\n\n");

                /* Child process serves request and terminates */
                if ((pid = fork()) < 0) {
                        server_error("fork()");
                        return EXIT_FAILURE;
                } else if (pid == 0) {
                        close(s.connfd);

		        get_request();

			server_out(OUT_INF, "From:\n%s\nReceived:\n%s", inet_ntoa(s.saddr.sin_addr), s.request);

                        tokenize_request(s.request);
			validate_request();

                        gen_successheader();
			send_response(s.hdr);

			for (; read(s.fd, s.body, CHUNKSZ) > 0; )
				send_response(s.body);

                        close(s.fd);
                        close(s.newfd);

		        server_out(OUT_DBG, "Sent:\n%s\n%s\n", s.hdr, s.body);

                        return EXIT_SUCCESS;
                }
		close(s.newfd);
	}

	return EXIT_SUCCESS;
}
