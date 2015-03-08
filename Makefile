
INSTALL_DIR?=/usr/local/bin

BIN:=minitel_display

CC:=$(CROSS_COMPILE)gcc
CFLAGS:= -Wall -std=gnu99 -Werror -Wextra

INSTALL_FILES:=$(BIN) $(wildcard scripts/*) 

all: $(BIN)

$(BIN): src/minitel_display.c
	$(CC) $(CFLAGS) -o $@ `pkg-config --cflags --libs MagickWand` $<

.PHONY: install clean

clean:
	rm -f $(BIN)

install:
	mkdir -p $(INSTALL_DIR)
	cp $(INSTALL_FILES) $(INSTALL_DIR)
