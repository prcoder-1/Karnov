# Karnov — C99 + SDL2.
#
#   karnov      — построчный перевод KARNOV.EXE (src/orig) + платформа
#   karnov_old  — прежний порт-реконструкция (src/old) + платформа
#
# Сборка: LC_ALL=C make

CC      ?= gcc
CFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -O2 -Iinclude -D_DEFAULT_SOURCE
CFLAGS  += -MMD -MP
LDLIBS  ?= -lSDL2 -lpthread -lm
PYTHON  ?= python3

PLAT_SRC := $(wildcard src/platform/*.c)
OLD_SRC  := $(wildcard src/old/*.c)
ORIG_SRC := $(filter-out src/orig/fnreg.c,$(wildcard src/orig/*.c))

PLAT_OBJ := $(PLAT_SRC:.c=.o)
OLD_OBJ  := $(OLD_SRC:.c=.o)
ORIG_OBJ := $(ORIG_SRC:.c=.o) src/orig/fnreg.o

all: karnov karnov_old

karnov: $(ORIG_OBJ) $(PLAT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

karnov_old: $(OLD_OBJ) $(PLAT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Прототипы и реестр функций — из тегов @SEG:OFF.
include/orig/fn.h src/orig/fnreg.c: $(ORIG_SRC) tools/gen_fn.py
	$(PYTHON) tools/gen_fn.py

$(ORIG_OBJ): include/orig/fn.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(wildcard src/*/*.d)

clean:
	rm -f src/*/*.o src/*/*.d karnov karnov_old

.PHONY: all clean
