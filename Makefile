CC      = gcc
CFLAGS  = -Wall -O2
TARGET  = winscope
SRC     = src/winscope.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.hive

.PHONY: all clean