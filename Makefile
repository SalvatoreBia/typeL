CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra -pedantic -pthread -D_XOPEN_SOURCE=700
LDFLAGS=
LIBS=-lcjson

# per macOS (commenta se non serve)
# CFLAGS += -I/opt/homebrew/include
# LDFLAGS += -L/opt/homebrew/lib

SRCS=backend.c network.c
OBJS=$(SRCS:.c=.o)
BIN=typeL-server

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)
