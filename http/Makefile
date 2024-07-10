#
# @author Andrey Proskurin 12122381
# @date   2022-12-04
# @brief  Makefile for http client and server
#
DEFS := -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
WARNINGS := -Wall -Wextra -Wpedantic
FLAGS := -std=c99 -pedantic $(WARNINGS) $(DEFS) -g -lz

# alias va='valgrind --leak-check=full --show-leak-kinds=all -s'

all: client server

client: client.c
	gcc -o $@ $(FLAGS) client.c

server: server.c
	gcc -o $@ $(FLAGS) server.c

clean:
	rm -f *.o client server

.PHONY: all clean
