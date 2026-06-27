# TerminalChat — server + client + framing test.
#
# Portable across Windows (MSYS2/MinGW) and POSIX. On Windows the sockets
# library (-lws2_32) is linked automatically; on Linux/macOS no extra libs
# are needed.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -Isrc
BIN     := bin

# Detect Windows (MSYS2/MinGW set OS=Windows_NT) and link Winsock there.
ifeq ($(OS),Windows_NT)
  LDLIBS  := -lws2_32
  EXE     := .exe
else
  LDLIBS  :=
  EXE     :=
endif

SERVER := $(BIN)/server$(EXE)
CLIENT := $(BIN)/client$(EXE)
TEST   := $(BIN)/test_framing$(EXE)

COMMON := src/common.c

.PHONY: all test clean

all: $(SERVER) $(CLIENT) $(TEST)

$(BIN):
	mkdir -p $(BIN)

$(SERVER): src/server.c $(COMMON) src/common.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/server.c $(COMMON) $(LDLIBS)

$(CLIENT): src/client.c $(COMMON) src/common.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/client.c $(COMMON) $(LDLIBS)

$(TEST): tests/test_framing.c $(COMMON) src/common.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ tests/test_framing.c $(COMMON) $(LDLIBS)

# build + run the unit tests
test: $(TEST)
	./$(TEST)

clean:
	rm -rf $(BIN)
