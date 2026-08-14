# Karnov — чистый C99 порт. Линкуется с SDL2.
#
# Сборка: make
# Запуск splash в SDL2: ./karnov
# Headless рендер кадра: ./karnov --ppm out.ppm --frame N

CC      ?= gcc
CFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -O2 -Iinclude -D_DEFAULT_SOURCE
CFLAGS  += -MMD -MP
LDLIBS  ?= -lSDL2 -lpthread -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

karnov: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEP)

clean:
	rm -f $(OBJ) $(DEP) karnov

.PHONY: clean
