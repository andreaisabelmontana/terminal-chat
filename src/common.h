/* common.h — shared framing and socket utilities for TerminalChat.
 *
 * Portable across Windows (Winsock2) and POSIX. Include this from both
 * server.c and client.c. The pure framing helpers (frame_message,
 * recv_line state machine) carry no platform dependencies and are
 * exercised directly by tests/test_framing.c.
 */
#ifndef TERMINALCHAT_COMMON_H
#define TERMINALCHAT_COMMON_H

#include <stddef.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET socket_t;
  #define TC_INVALID_SOCKET INVALID_SOCKET
  #define TC_SOCKET_ERROR   SOCKET_ERROR
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int socket_t;
  #define TC_INVALID_SOCKET (-1)
  #define TC_SOCKET_ERROR   (-1)
#endif

/* Wire limits. Messages are newline-delimited ('\n'). A single logical
 * line may not exceed MAX_LINE bytes including the terminator. */
#define MAX_LINE   1024
#define MAX_NICK   32
#define MAX_CLIENTS 64

/* ---- platform init / teardown ---- */
int  net_init(void);     /* WSAStartup on Windows, no-op on POSIX. 0 on success. */
void net_cleanup(void);  /* WSACleanup on Windows, no-op on POSIX. */
void close_socket(socket_t s);

/* ---- pure framing helpers (no networking; unit-tested) ---- */

/* Copy `msg` into `out` (size `outsz`) and ensure it ends in exactly one
 * '\n'. Any embedded newlines/carriage-returns in `msg` are stripped so a
 * single logical message can never be split into two frames. Returns the
 * number of bytes written (excluding the NUL terminator), or -1 if the
 * result would not fit. `out` is always NUL-terminated on success. */
int frame_message(const char *msg, char *out, size_t outsz);

/* A line-reassembly buffer. Bytes arrive from the socket in arbitrary
 * chunks; feed them in and pull out whole '\n'-terminated lines. */
typedef struct {
    char   data[MAX_LINE * 2];
    size_t len;
} line_buf_t;

void lb_init(line_buf_t *lb);

/* Append `n` bytes from `chunk` to the buffer. Returns 0 on success,
 * -1 if the buffer would overflow (line too long / no newline). */
int lb_push(line_buf_t *lb, const char *chunk, size_t n);

/* Extract the next complete line (without its '\n') into `out` (size
 * `outsz`), removing it from the buffer. Returns the line length on
 * success, 0 if no complete line is buffered yet, -1 if a line was found
 * but does not fit in `out`. */
int lb_next_line(line_buf_t *lb, char *out, size_t outsz);

/* ---- socket I/O helpers ---- */

/* Send all `len` bytes of `buf`, looping over partial sends. Returns 0 on
 * success, -1 on error/closed connection. */
int send_all(socket_t s, const char *buf, size_t len);

/* Convenience: frame `msg` (newline-terminate) and send it whole. */
int send_line(socket_t s, const char *msg);

#endif /* TERMINALCHAT_COMMON_H */
