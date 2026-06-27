/* common.c — implementation of shared framing and socket utilities. */
#include "common.h"

#include <string.h>

#ifdef _WIN32
  /* link with -lws2_32 */
#else
  #include <errno.h>
#endif

int net_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void close_socket(socket_t s)
{
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

int frame_message(const char *msg, char *out, size_t outsz)
{
    size_t w = 0;
    if (out == NULL || outsz < 2) return -1;
    for (const char *p = msg; *p; ++p) {
        if (*p == '\n' || *p == '\r') continue;   /* never split a frame */
        if (w + 2 > outsz) return -1;              /* leave room for '\n' + NUL */
        out[w++] = *p;
    }
    out[w++] = '\n';
    out[w] = '\0';
    return (int)w;
}

void lb_init(line_buf_t *lb)
{
    lb->len = 0;
}

int lb_push(line_buf_t *lb, const char *chunk, size_t n)
{
    if (lb->len + n > sizeof(lb->data)) return -1;
    memcpy(lb->data + lb->len, chunk, n);
    lb->len += n;
    return 0;
}

int lb_next_line(line_buf_t *lb, char *out, size_t outsz)
{
    char *nl = memchr(lb->data, '\n', lb->len);
    if (nl == NULL) return 0;                 /* no full line yet */

    size_t line_len = (size_t)(nl - lb->data);
    /* trim a trailing '\r' so CRLF clients behave */
    size_t copy_len = line_len;
    if (copy_len > 0 && lb->data[copy_len - 1] == '\r') copy_len--;

    if (copy_len + 1 > outsz) {
        /* drop the offending line so the buffer doesn't wedge */
        size_t rest = lb->len - (line_len + 1);
        memmove(lb->data, nl + 1, rest);
        lb->len = rest;
        return -1;
    }

    memcpy(out, lb->data, copy_len);
    out[copy_len] = '\0';

    /* shift the consumed line (including its '\n') out of the buffer */
    size_t rest = lb->len - (line_len + 1);
    memmove(lb->data, nl + 1, rest);
    lb->len = rest;

    return (int)copy_len;
}

int send_all(socket_t s, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, (int)(len - sent), 0);
        if (n == TC_SOCKET_ERROR || n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

int send_line(socket_t s, const char *msg)
{
    char frame[MAX_LINE];
    int n = frame_message(msg, frame, sizeof(frame));
    if (n < 0) return -1;
    return send_all(s, frame, (size_t)n);
}
