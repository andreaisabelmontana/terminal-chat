/* test_framing.c — unit tests for the pure framing helpers in common.c.
 *
 * No networking involved: this exercises frame_message() and the
 * line-reassembly buffer (lb_push / lb_next_line) that turn an arbitrary
 * stream of bytes back into whole messages. Builds and runs standalone.
 * Exits 0 if every assertion holds.
 */
#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests = 0;
#define CHECK(cond) do { tests++; assert(cond); } while (0)

static void test_frame_basic(void)
{
    char out[MAX_LINE];
    int n = frame_message("hello", out, sizeof(out));
    CHECK(n == 6);
    CHECK(strcmp(out, "hello\n") == 0);
}

static void test_frame_strips_embedded_newlines(void)
{
    /* embedded \n and \r must be removed so one message stays one frame */
    char out[MAX_LINE];
    int n = frame_message("a\nb\r\nc", out, sizeof(out));
    CHECK(n == 4);
    CHECK(strcmp(out, "abc\n") == 0);
}

static void test_frame_rejects_overflow(void)
{
    char out[8];
    int n = frame_message("this is way too long for the buffer", out, sizeof(out));
    CHECK(n == -1);
}

static void test_frame_empty(void)
{
    char out[MAX_LINE];
    int n = frame_message("", out, sizeof(out));
    CHECK(n == 1);
    CHECK(strcmp(out, "\n") == 0);
}

static void test_reassembly_simple(void)
{
    line_buf_t lb;
    lb_init(&lb);
    lb_push(&lb, "hi\n", 3);

    char line[MAX_LINE];
    int r = lb_next_line(&lb, line, sizeof(line));
    CHECK(r == 2);
    CHECK(strcmp(line, "hi") == 0);
    /* nothing left */
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 0);
}

static void test_reassembly_partial_then_complete(void)
{
    /* a message split across two recv() chunks must reassemble */
    line_buf_t lb;
    lb_init(&lb);
    char line[MAX_LINE];

    lb_push(&lb, "par", 3);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 0);   /* not yet */

    lb_push(&lb, "tial\n", 5);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 7);
    CHECK(strcmp(line, "partial") == 0);
}

static void test_reassembly_multiple_in_one_chunk(void)
{
    /* two messages arriving glued together must split into two lines */
    line_buf_t lb;
    lb_init(&lb);
    char line[MAX_LINE];

    lb_push(&lb, "one\ntwo\n", 8);

    CHECK(lb_next_line(&lb, line, sizeof(line)) == 3);
    CHECK(strcmp(line, "one") == 0);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 3);
    CHECK(strcmp(line, "two") == 0);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 0);
}

static void test_reassembly_crlf(void)
{
    /* a CRLF client: the trailing \r is trimmed */
    line_buf_t lb;
    lb_init(&lb);
    char line[MAX_LINE];

    lb_push(&lb, "win\r\n", 5);
    int r = lb_next_line(&lb, line, sizeof(line));
    CHECK(r == 3);
    CHECK(strcmp(line, "win") == 0);
}

static void test_roundtrip(void)
{
    /* frame three messages, push them as one blob, recover all three */
    char a[MAX_LINE], b[MAX_LINE], c[MAX_LINE];
    frame_message("alice", a, sizeof(a));
    frame_message("bob",   b, sizeof(b));
    frame_message("carol", c, sizeof(c));

    line_buf_t lb;
    lb_init(&lb);
    lb_push(&lb, a, strlen(a));
    lb_push(&lb, b, strlen(b));
    lb_push(&lb, c, strlen(c));

    char line[MAX_LINE];
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 5 && strcmp(line, "alice") == 0);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 3 && strcmp(line, "bob")   == 0);
    CHECK(lb_next_line(&lb, line, sizeof(line)) == 5 && strcmp(line, "carol") == 0);
}

int main(void)
{
    test_frame_basic();
    test_frame_strips_embedded_newlines();
    test_frame_rejects_overflow();
    test_frame_empty();
    test_reassembly_simple();
    test_reassembly_partial_then_complete();
    test_reassembly_multiple_in_one_chunk();
    test_reassembly_crlf();
    test_roundtrip();

    printf("test_framing: all %d assertions passed\n", tests);
    return 0;
}
