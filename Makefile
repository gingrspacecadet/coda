CC = gcc
CFLAGS = -g -Werror -MMD -MD -std=gnu23 -O0

SRCS = $(shell find src/ -type f -name "*.c" 2>/dev/null)
OBJS = $(patsubst %.c,%.o,$(SRCS))

PCH = src/pch.hpp
PCH_GCH = $(PCH).gch

TARGET = coda

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# $(PCH_GCH): $(PCH)
# 	$(CC) $(CFLAGS) -x c++-header -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@find -type f -regex '.*\.\(o\|d\|gch\)' -delete
	@rm -f $(TARGET)

-include $(patsubst %.o,%.d,$(OBJS))