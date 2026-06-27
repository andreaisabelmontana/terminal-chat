#!/usr/bin/env bash
# smoke.sh — end-to-end test: start the server, connect two clients, and
# confirm a message typed by one client is broadcast to the other.
#
# Uses the client's --batch mode (non-interactive: pipes lines in, prints
# whatever the server broadcasts back) so the whole exchange is scriptable
# with no TTY. Works on Windows (MSYS2) and Linux alike.
#
# Run from the repo root:  bash tests/smoke.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# pick the right binary suffix
if [ -x bin/server.exe ]; then EXE=.exe; else EXE=; fi
SERVER="bin/server$EXE"
CLIENT="bin/client$EXE"
PORT=5555

echo "=== TerminalChat smoke test ==="
echo "starting server on port $PORT"
"$SERVER" "$PORT" &
SERVER_PID=$!
sleep 1

# Listener: nickname "bob", then sit quietly so it receives whatever is
# broadcast. The trailing sleep keeps bob connected long enough to hear
# alice. Output is captured to bob.out.
( printf 'bob\n'; sleep 2 ) | "$CLIENT" 127.0.0.1 "$PORT" --batch > bob.out 2>&1 &
BOB_PID=$!
sleep 1   # let bob join first

# Sender: nickname "alice", then send one chat line.
printf 'alice\nhello from alice\n' | "$CLIENT" 127.0.0.1 "$PORT" --batch > alice.out 2>&1

wait $BOB_PID 2>/dev/null

echo
echo "--- alice's view (the sender) ---"
cat alice.out
echo "--- bob's view (the receiver) ---"
cat bob.out
echo "---------------------------------"

# shut the server down
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo
if grep -q "alice: hello from alice" bob.out; then
    echo "RESULT: PASS — bob received alice's broadcast"
    rc=0
else
    echo "RESULT: FAIL — broadcast not seen by bob"
    rc=1
fi
rm -f alice.out bob.out
exit $rc
