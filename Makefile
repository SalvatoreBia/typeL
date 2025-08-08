CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lcjson -lpthread
TARGET = typer

OBJS = backend.o network.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(TARGET)
