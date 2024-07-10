#
# @author Andrey Proskurin 12122381
# @date   2022-10-26
# @brief  Makefile for forksort
#
DEFS := -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
WARNINGS := -Wall -Wextra -Wpedantic
FLAGS := -std=c99 -pedantic $(WARNINGS) $(DEFS) -g

# alias va='valgrind --leak-check=full --show-leak-kinds=all -s'

all: forksort bonus

forksort: forksort.c
	gcc -o $@ $(FLAGS) forksort.c

bonus: forksort.c
	gcc -o $@ $(FLAGS) -DOSUETREE forksort.c

clean:
	rm -f *.o forksort bonus

.PHONY: all clean
