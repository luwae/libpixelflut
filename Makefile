CC = gcc
CFLAGS = -Wall -Wextra -g

PROGS = minimal

all: pixelflut.o $(PROGS)

minimal: minimal.o pixelflut.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
