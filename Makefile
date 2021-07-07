CC=gcc
CFLAGS=-std=c89 -O3 -Wall

all: main.c
	$(CC) $(CFLAGS) -o zbf $^

clean: zbf
	rm $<

