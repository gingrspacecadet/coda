CC = gcc
CFLAGS = -g -Werror -MMD -MD -std=gnu23 -O0

SRCS = $(shell find src -type f -name "*.c" ! -path "src/backends/*" 2>/dev/null)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))

TARGET = coda

BACKEND_DIRS = $(shell find src/backends -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null)
BACKEND_SRCS = $(shell find src/backends -type f -name "*.c" 2>/dev/null)
BACKEND_OBJS = $(patsubst src/%.c,build/%.o,$(BACKEND_SRCS))
BACKENDS_SO = $(patsubst %,$(addprefix build/backends/,%.$(SOEXT)),$(BACKEND_DIRS))

ifeq ($(OS),Windows_NT)
	SOEXT = dll
	SHARED_FLAGS =
	DLL_LINK = -shared
else
	SOEXT = so
	SHARED_FLAGS = -fPIC
	DLL_LINK = -shared
endif

.PHONY: all clean backends

all: $(TARGET) backends

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -rdynamic

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/backends/%.o: src/backends/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -c -o $@ $<

$(foreach d,$(BACKEND_DIRS), \
  $(eval BACKEND_SRCS_$(d) := $(wildcard src/backends/$(d)/*.c)) \
  $(eval BACKEND_OBJS_$(d) := $(patsubst src/%.c,build/%.o,$(BACKEND_SRCS_$(d)))) \
  $(eval build/backends/$(d).$(SOEXT): $$(BACKEND_OBJS_$(d))) \
  $(eval build/backends/$(d).$(SOEXT): ; \
	@mkdir -p build/backends/$(d) ; \
	if [ -n "$$^" ]; then \
		echo "$(CC) $(DLL_LINK) -o $$@ $$^"; \
		$(CC) $(DLL_LINK) -o $$@ $$^; \
	fi ) \
)

backends: $(BACKENDS_SO)

clean:
	@rm -rf build
	@rm -f $(TARGET)

-include $(patsubst %.o,%.d,$(OBJS) $(BACKEND_OBJS))
