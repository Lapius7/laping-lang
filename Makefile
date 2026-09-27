CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS = -lcurl -lm -lpthread

SRCS = src/main.c src/lexer.c src/parser.c src/value.c src/interp.c src/builtins.c src/updater.c
HDRS = src/laping.h src/updater.h

laping: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o laping $(SRCS) $(LDLIBS)

test: laping
	./tests/run_tests.sh ./laping

install: laping
	mkdir -p $(HOME)/.local/bin
	cp laping $(HOME)/.local/bin/laping

clean:
	rm -f laping laping.exe

.PHONY: test install clean
