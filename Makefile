CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c23
LDFLAGS = -lcrypto -lbsd

BUILDDIR = ./build
BINDIR = $(BUILDDIR)/bin
SERVERBIN = $(BINDIR)/backupd
CLIENTBIN = $(BINDIR)/client

all: server client

server: src/server.c
	@mkdir -p $(BINDIR)
	$(CC) src/server.c $(CFLAGS) $(LDFLAGS) -o $(SERVERBIN)

client: src/client.c
	@mkdir -p $(BINDIR)
	$(CC) src/client.c $(CFLAGS) $(LDFLAGS) -o $(CLIENTBIN)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all server client clean
