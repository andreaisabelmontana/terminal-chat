/* client.c — TerminalChat terminal client.
 *
 * Connects to the server, sends the lines you type, and prints every
 * broadcast it receives. Two modes:
 *
 *   interactive (default)
 *       Reads your keystrokes and the socket at the same time. On POSIX this
 *       is a single select() over stdin + the socket. On Windows stdin is not
 *       a selectable handle, so a small receiver thread prints incoming
 *       messages while the main thread reads your input.
 *
 *   --batch
 *       Non-interactive, for scripting and the smoke test. Reads all of stdin
 *       (piped lines), sends them, then drains the socket for a short grace
 *       period and prints whatever arrived. Deterministic, no TTY needed.
 *
 * Usage:  client [host] [port] [--batch]
 *         (defaults: 127.0.0.1 5050)
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <process.h>
#else
  #include <sys/select.h>
  #include <sys/time.h>
#endif

static socket_t connect_to(const char *host, int port)
{
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == TC_INVALID_SOCKET) return TC_INVALID_SOCKET;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close_socket(s);
        return TC_INVALID_SOCKET;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == TC_SOCKET_ERROR) {
        close_socket(s);
        return TC_INVALID_SOCKET;
    }
    return s;
}

/* print every complete line currently sitting in `lb` */
static void drain_lines(line_buf_t *lb)
{
    char line[MAX_LINE];
    int r;
    while ((r = lb_next_line(lb, line, sizeof(line))) > 0) {
        printf("%s\n", line);
        fflush(stdout);
    }
}

/* ---------------- batch mode: deterministic, used by the smoke test ------- */
static int run_batch(socket_t s)
{
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), stdin)) {
        if (send_line(s, line) < 0) break;     /* send_line strips the '\n' */
    }

    /* read replies for a short grace window so broadcasts have time to land */
    line_buf_t lb;
    lb_init(&lb);

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 400000;                    /* 400 ms idle = done */
#ifdef _WIN32
        int ready = select(0, &rfds, NULL, NULL, &tv);
#else
        int ready = select((int)s + 1, &rfds, NULL, NULL, &tv);
#endif
        if (ready <= 0) break;                  /* timeout or error */

        char chunk[512];
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        if (lb_push(&lb, chunk, (size_t)n) == 0) drain_lines(&lb);
    }
    return 0;
}

/* ---------------- interactive mode --------------------------------------- */
#ifdef _WIN32
static socket_t g_sock;
static volatile int g_running = 1;

static unsigned __stdcall recv_thread(void *arg)
{
    (void)arg;
    line_buf_t lb;
    lb_init(&lb);
    char chunk[512];
    while (g_running) {
        int n = recv(g_sock, chunk, sizeof(chunk), 0);
        if (n <= 0) { g_running = 0; break; }
        if (lb_push(&lb, chunk, (size_t)n) == 0) drain_lines(&lb);
    }
    printf("* disconnected\n");
    fflush(stdout);
    return 0;
}

static int run_interactive(socket_t s)
{
    g_sock = s;
    g_running = 1;
    uintptr_t th = _beginthreadex(NULL, 0, recv_thread, NULL, 0, NULL);

    char line[MAX_LINE];
    while (g_running && fgets(line, sizeof(line), stdin)) {
        if (send_line(s, line) < 0) break;
        if (strncmp(line, "/quit", 5) == 0) break;
    }
    g_running = 0;
    close_socket(s);
    if (th) WaitForSingleObject((HANDLE)th, 500);
    return 0;
}
#else
static int run_interactive(socket_t s)
{
    line_buf_t lb;
    lb_init(&lb);

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        FD_SET(0, &rfds);                       /* stdin */
        int maxfd = (int)s > 0 ? (int)s : 0;

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(s, &rfds)) {
            char chunk[512];
            int n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) { printf("* disconnected\n"); break; }
            if (lb_push(&lb, chunk, (size_t)n) == 0) drain_lines(&lb);
        }
        if (FD_ISSET(0, &rfds)) {
            char line[MAX_LINE];
            if (!fgets(line, sizeof(line), stdin)) break;
            if (send_line(s, line) < 0) break;
            if (strncmp(line, "/quit", 5) == 0) break;
        }
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int   port  = 5050;
    int   batch = 0;

    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--batch") == 0) { batch = 1; continue; }
        if (pos == 0)      { host = argv[i]; pos++; }
        else if (pos == 1) { port = atoi(argv[i]); pos++; }
    }

    if (net_init() != 0) {
        fprintf(stderr, "net_init failed\n");
        return 1;
    }

    socket_t s = connect_to(host, port);
    if (s == TC_INVALID_SOCKET) {
        fprintf(stderr, "could not connect to %s:%d\n", host, port);
        net_cleanup();
        return 1;
    }

    int rc = batch ? run_batch(s) : run_interactive(s);

    close_socket(s);
    net_cleanup();
    return rc;
}
