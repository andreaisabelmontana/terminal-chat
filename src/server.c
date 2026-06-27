/* server.c — TerminalChat server.
 *
 * A single-threaded TCP chat server built on a select() event loop, so it
 * stays portable between Windows (Winsock2) and POSIX without pulling in
 * pthreads. It accepts many clients at once, tracks a nickname per client,
 * reassembles newline-delimited messages from partial reads, and broadcasts
 * each line to every other connected client. Join/leave are announced and
 * dropped clients are cleaned up without taking the server down.
 *
 * Usage:  server [port]      (default port 5050)
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    socket_t   fd;
    char       nick[MAX_NICK];
    int        named;          /* 0 until the first line sets the nickname */
    line_buf_t in;
} client_t;

static client_t clients[MAX_CLIENTS];
static int      nclients = 0;

static int port_from_args(int argc, char **argv)
{
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536) return p;
    }
    return 5050;
}

/* send a server line to one client (ignoring errors — caller reaps dead fds) */
static void tell(socket_t fd, const char *msg)
{
    send_line(fd, msg);
}

/* broadcast `msg` to every client except `except` (pass TC_INVALID_SOCKET
 * to reach everyone). The message is framed once and sent to each. */
static void broadcast(const char *msg, socket_t except)
{
    char frame[MAX_LINE];
    if (frame_message(msg, frame, sizeof(frame)) < 0) return;
    for (int i = 0; i < nclients; ++i) {
        if (clients[i].fd == except) continue;
        send_all(clients[i].fd, frame, strlen(frame));
    }
}

static void add_client(socket_t fd)
{
    client_t *c = &clients[nclients++];
    c->fd = fd;
    c->named = 0;
    c->nick[0] = '\0';
    lb_init(&c->in);
    tell(fd, "* welcome to TerminalChat — type your nickname:");
}

static void remove_client(int idx)
{
    client_t *c = &clients[idx];
    if (c->named) {
        char line[MAX_LINE];
        snprintf(line, sizeof(line), "* %s left the chat", c->nick);
        close_socket(c->fd);
        /* compact before broadcasting so we don't message the dead fd */
        clients[idx] = clients[--nclients];
        broadcast(line, TC_INVALID_SOCKET);
    } else {
        close_socket(c->fd);
        clients[idx] = clients[--nclients];
    }
}

/* handle one complete line from client `idx`. Returns 0 to keep the client,
 * -1 to drop it. */
static int handle_line(int idx, const char *line)
{
    client_t *c = &clients[idx];

    if (!c->named) {
        if (line[0] == '\0') {
            tell(c->fd, "* nickname cannot be empty — try again:");
            return 0;
        }
        size_t nlen = strlen(line);
        if (nlen > MAX_NICK - 1) nlen = MAX_NICK - 1;   /* truncate long nicks */
        memcpy(c->nick, line, nlen);
        c->nick[nlen] = '\0';
        c->named = 1;

        char join[MAX_LINE];
        snprintf(join, sizeof(join), "* %s joined the chat", c->nick);
        broadcast(join, c->fd);
        snprintf(join, sizeof(join), "* you are now known as %s", c->nick);
        tell(c->fd, join);
        return 0;
    }

    if (strcmp(line, "/quit") == 0) return -1;

    /* "nick: " prefix (nick <= MAX_NICK-1) + line (<= MAX_LINE-1) + NUL */
    char out[MAX_NICK + MAX_LINE + 4];
    snprintf(out, sizeof(out), "%s: %s", c->nick, line);
    broadcast(out, c->fd);          /* relay to everyone but the sender */
    return 0;
}

/* drain whatever bytes a ready client has, parsing out whole lines.
 * Returns 0 to keep the client, -1 to drop it. */
static int service_client(int idx)
{
    client_t *c = &clients[idx];
    char chunk[512];
    int n = recv(c->fd, chunk, sizeof(chunk), 0);
    if (n <= 0) return -1;          /* closed or error */

    if (lb_push(&c->in, chunk, (size_t)n) < 0) {
        tell(c->fd, "* line too long — dropping connection");
        return -1;
    }

    char line[MAX_LINE];
    int r;
    while ((r = lb_next_line(&c->in, line, sizeof(line))) != 0) {
        if (r < 0) {                 /* over-long single line; skip it */
            tell(c->fd, "* message too long — ignored");
            continue;
        }
        if (handle_line(idx, line) < 0) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int port = port_from_args(argc, argv);

    if (net_init() != 0) {
        fprintf(stderr, "net_init failed\n");
        return 1;
    }

    socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == TC_INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        net_cleanup();
        return 1;
    }

    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((unsigned short)port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == TC_SOCKET_ERROR) {
        fprintf(stderr, "bind() failed on port %d\n", port);
        close_socket(listener);
        net_cleanup();
        return 1;
    }
    if (listen(listener, 16) == TC_SOCKET_ERROR) {
        fprintf(stderr, "listen() failed\n");
        close_socket(listener);
        net_cleanup();
        return 1;
    }

    printf("TerminalChat server listening on port %d\n", port);
    fflush(stdout);

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listener, &rfds);

#ifndef _WIN32
        int maxfd = listener;
#endif
        for (int i = 0; i < nclients; ++i) {
            FD_SET(clients[i].fd, &rfds);
#ifndef _WIN32
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
#endif
        }

#ifdef _WIN32
        int ready = select(0, &rfds, NULL, NULL, NULL);
#else
        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
#endif
        if (ready == TC_SOCKET_ERROR) {
            fprintf(stderr, "select() failed\n");
            break;
        }

        /* new connection */
        if (FD_ISSET(listener, &rfds)) {
            socket_t fd = accept(listener, NULL, NULL);
            if (fd != TC_INVALID_SOCKET) {
                if (nclients < MAX_CLIENTS) {
                    add_client(fd);
                } else {
                    tell(fd, "* server full — try again later");
                    close_socket(fd);
                }
            }
        }

        /* service ready clients (iterate backwards: remove_client compacts) */
        for (int i = nclients - 1; i >= 0; --i) {
            if (i >= nclients) continue;
            if (FD_ISSET(clients[i].fd, &rfds)) {
                if (service_client(i) < 0) remove_client(i);
            }
        }
    }

    for (int i = 0; i < nclients; ++i) close_socket(clients[i].fd);
    close_socket(listener);
    net_cleanup();
    return 0;
}
