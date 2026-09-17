CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
PREFIX  ?= /usr/local
BINDIR   = $(DESTDIR)$(PREFIX)/bin
TARGET   = winscope
SRC      = src/winscope.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	install -d "$(BINDIR)"
	install -m 755 $(TARGET) "$(BINDIR)/"

uninstall:
	rm -f "$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
