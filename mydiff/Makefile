#
# @author Andrey Proskurin 12122381
# @date   2022-10-23
# @brief  Makefile for mydiff
#
DEFS := -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
WARNINGS := -Wall -Wextra -Wpedantic
FLAGS := -std=c99 -pedantic $(WARNINGS) $(DEFS) -g

# alias va='valgrind --leak-check=full --show-leak-kinds=all -s'

all:
	gcc -o mydiff $(FLAGS) mydiff.c

clean:
	rm -f *.o mydiff

.PHONY: all clean
