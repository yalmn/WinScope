CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
PREFIX  ?= /usr/local
BINDIR   = $(DESTDIR)$(PREFIX)/bin
TARGET   = winscope
SRC      = src/winscope.c

all: $(TARGET)

tests/unit_tests: tests/unit_tests.c $(SRC)
	$(CC) $(CFLAGS) -o $@ tests/unit_tests.c

test: tests/unit_tests
	./tests/unit_tests

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	install -d "$(BINDIR)"
	install -m 755 $(TARGET) "$(BINDIR)/"

uninstall:
	rm -f "$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET) tests/unit_tests

.PHONY: all test install uninstall clean
