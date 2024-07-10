#
# @author Andrey Proskurin 12122381
# @date   2022-10-23
# @brief  Makefile for 3color
#
DEFS := -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
WARNINGS := -Wall -Wextra -Wpedantic
FLAGS := -std=c99 -pedantic $(WARNINGS) $(DEFS) -g -lrt -pthread

# alias va='valgrind --leak-check=full --show-leak-kinds=all -s'

all: generator supervisor

generator: generator.o
	gcc -o $@ $(FLAGS) $<

supervisor: supervisor.o
	gcc -o $@ $(FLAGS) $<

generator.o: generator.c common.h
	gcc -o $@ $(FLAGS) -c generator.c

supervisor.o: supervisor.c common.h
	gcc -o $@ $(FLAGS) -c supervisor.c

clean:
	rm -f *.o generator supervisor

.PHONY: all clean
