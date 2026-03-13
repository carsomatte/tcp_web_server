CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS=-pthread
program: main.o
	$(CC) $(CFLAGS) main.o -o server

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f *.o server
