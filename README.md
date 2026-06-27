# TerminalChat

A terminal-based chat client and server written in C. One server, many clients,
real-time broadcast — built directly on TCP sockets with a `select()` event loop,
no threads-per-client, no frameworks. Portable across **Windows** (Winsock2) and
**POSIX** (Linux/macOS) from a single codebase.

- **Live site:** https://andreaisabelmontana.github.io/terminal-chat/

## What it does

Start the server, point clients at it, and everyone in the room sees each
message as it's sent. The server tracks a nickname per connection, announces
joins and leaves, relays each chat line to every *other* client, and cleans up
after clients that disconnect — all on a single thread.

## Architecture

```
                 ┌─────────────────────────────────────┐
   client A ───► │  server: one select() loop           │
   client B ───► │   • listener socket (accept)         │ ──► broadcast to
   client C ───► │   • per-client recv → line buffer    │     all others
                 │   • whole line → "nick: message"     │
                 └─────────────────────────────────────┘
```

**`select()`-based server (`src/server.c`).**
One event loop watches the listener socket plus every connected client at once.
`select()` reports which sockets are ready; the server accepts new connections,
reads from whichever client spoke, and never blocks on a single slow client.
This is the classic single-threaded concurrency model — portable to Windows and
POSIX without pthreads. Capacity is `MAX_CLIENTS` (64) by default.

**Message framing (`src/common.c` / `common.h`).**
TCP is a byte stream, not a message stream: a `recv()` can return half a line,
or two lines glued together. Messages are **newline-delimited**. Incoming bytes
are pushed into a per-client `line_buf_t` reassembly buffer; `lb_next_line()`
pulls out complete `'\n'`-terminated lines (trimming a trailing `\r` so CRLF
clients work). Outgoing messages go through `frame_message()`, which strips any
embedded newlines so one logical message can never be split into two frames.
These helpers are pure functions with no networking — which is exactly what the
unit test exercises.

**Broadcast.**
When a client sends a complete line, the server formats it as `nick: message`
and sends it to every other client (the sender is skipped). Join/leave notices
(`* alice joined the chat`) are broadcast the same way.

**Disconnect handling.**
A `recv()` returning `<= 0` means the client closed or errored. The server
closes that socket, removes the client from its list (compacting the array),
and broadcasts a leave notice — without disturbing anyone else.

**Client (`src/client.c`).**
The client has to read your keystrokes and the socket at the same time:

- *Interactive (default).* On **POSIX**, a single `select()` watches both
  `stdin` and the socket. On **Windows**, `stdin` is not a selectable handle, so
  a small receiver thread prints incoming broadcasts while the main thread reads
  your input. Type a line and press Enter to send; type `/quit` to leave.
- *Batch (`--batch`).* Non-interactive mode for scripting and the smoke test:
  it pipes in lines from `stdin`, sends them, then drains the socket for a short
  grace window and prints whatever was broadcast back. Deterministic, no TTY
  needed.

## Build

Needs a C11 compiler. The `Makefile` links `-lws2_32` automatically on Windows.

```sh
make            # builds bin/server, bin/client, bin/test_framing
make test       # builds and runs the framing unit test
```

No `make`? Compile directly (this is exactly what the Makefile runs):

```sh
# Windows (MSYS2 / MinGW) — note the -lws2_32
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/server.exe       src/server.c       src/common.c -lws2_32
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/client.exe       src/client.c       src/common.c -lws2_32
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/test_framing.exe tests/test_framing.c src/common.c -lws2_32

# Linux / macOS — no extra libs
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/server       src/server.c       src/common.c
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/client       src/client.c       src/common.c
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/test_framing tests/test_framing.c src/common.c
```

## Run

Open one terminal for the server and one per client.

```sh
./bin/server 5050              # listen on port 5050 (default if omitted)

./bin/client 127.0.0.1 5050    # in another terminal
```

On connect the client asks for a nickname (your first line). After that, every
line you type is broadcast to the room. `/quit` disconnects.

## Tests

### Framing unit test

`tests/test_framing.c` exercises the pure framing/reassembly helpers — framing,
overflow rejection, partial reads reassembled across chunks, multiple messages
in one chunk, CRLF trimming, and a round trip — with `assert()`. Real output:

```
$ gcc -O2 -Wall -Wextra -std=c11 -Isrc -o bin/test_framing.exe tests/test_framing.c src/common.c -lws2_32
$ ./bin/test_framing.exe
test_framing: all 23 assertions passed
```

Exit code: `0`.

### End-to-end smoke test

`tests/smoke.sh` builds nothing itself — it starts the server, connects two
`--batch` clients (`bob` listens, `alice` sends one line), and checks that
alice's message was broadcast to bob. Real transcript on Windows (MSYS2):

```
$ bash tests/smoke.sh
=== TerminalChat smoke test ===
starting server on port 5555
TerminalChat server listening on port 5555

--- alice's view (the sender) ---
* welcome to TerminalChat — type your nickname:
* you are now known as alice
--- bob's view (the receiver) ---
* welcome to TerminalChat — type your nickname:
* you are now known as bob
* alice joined the chat
alice: hello from alice
* alice left the chat
---------------------------------

RESULT: PASS — bob received alice's broadcast
```

bob — a second, independent client process — received `alice: hello from alice`
plus the join/leave notices, confirming the full path: connect → nickname →
broadcast → disconnect.

## Layout

```
src/common.h        framing + socket-util declarations, platform shims
src/common.c        frame_message, line buffer, send_all/send_line, net_init
src/server.c        select() event loop, client list, broadcast
src/client.c        interactive (select/thread) + --batch modes
tests/test_framing.c  unit tests for the framing helpers (no networking)
tests/smoke.sh        end-to-end: server + two clients exchange a message
Makefile            builds all three, links -lws2_32 on Windows
```

## License

MIT — see [LICENSE](LICENSE).
