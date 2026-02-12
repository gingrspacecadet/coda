CC = gcc
CFLAGS = -g

SRCS = $(shell find src/ -type f -name "*.c" 2>/dev/null)
OBJS = $(patsubst %.c,%.o,$(SRCS))

TARGET = coda

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@find -type f -name *.o -delete
	@rm -f $(TARGET)