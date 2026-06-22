CC = gcc
CFLAGS = -O2 -Wall -Wextra
LDLIBS = -lcurl

laping: laping.c updater.c updater.h
	$(CC) $(CFLAGS) -o laping laping.c updater.c $(LDLIBS)

install: laping
	mkdir -p $(HOME)/.local/bin
	cp laping $(HOME)/.local/bin/laping

clean:
	rm -f laping laping.exe

.PHONY: install clean
