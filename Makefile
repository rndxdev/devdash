CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -MMD -MP $(shell pkg-config --cflags gtk+-3.0 libnotify vte-2.91)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 libnotify vte-2.91) -lm

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)
DEP     = $(OBJ:.o=.d)
BIN     = devdash

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

-include $(DEP)

clean:
	rm -rf build $(BIN)

install: $(BIN)
	cp $(BIN) /usr/local/bin/

.PHONY: clean install
