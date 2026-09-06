CC = gcc
CFLAGS = -g -Werror -Wextra -Wall -Wpedantic -Wno-unused -Wno-switch -MMD -std=gnu17 -O0 # -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer

SRC := src

SRCS = $(shell find $(SRC) -type f -name "*.c" ! -path "$(SRC)/backends/*" 2>/dev/null)
OBJS = $(patsubst $(SRC)/%.c,build/%.o,$(SRCS))

TARGET = codac

BACKEND_DIRS = $(shell find $(SRC)/backends -mindepth 1 -maxdepth 1 -type d | sed 's|.*/||')
BACKEND_SRCS = $(shell find $(SRC)/backends -type f -name "*.c" 2>/dev/null)
BACKEND_OBJS = $(patsubst $(SRC)/%.c,build/%.o,$(BACKEND_SRCS))
BACKENDS_SO  = $(patsubst %,$(addprefix build/backends/,%.$(SOEXT)),$(BACKEND_DIRS))

ifeq ($(OS),Windows_NT)
    SOEXT = dll
    SHARED_FLAGS =
    DLL_LINK = -shared
    HOST_OS = windows

    ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
        HOST_ARCH = x86_64
    else ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
        HOST_ARCH = aarch64
    else
        HOST_ARCH = unknown
    endif

else
    UNAME_S := $(shell uname -s)
    UNAME_M := $(shell uname -m)

    ifeq ($(UNAME_S),Darwin)
        HOST_OS = macos
        SOEXT = dylib
        SHARED_FLAGS = -fPIC
        DLL_LINK = -dynamiclib -undefined dynamic_lookup
    else
        HOST_OS = linux
        SOEXT = so
        SHARED_FLAGS = -fPIC
        DLL_LINK = -shared
    endif
    
    ifeq ($(UNAME_M),x86_64)
        HOST_ARCH = x86_64
    else ifeq ($(UNAME_M),arm64)
        HOST_ARCH = aarch64
    else ifeq ($(UNAME_M),aarch64)
        HOST_ARCH = aarch64
    else
        HOST_ARCH = unknown
    endif
endif

STDLIB_CODA_SRCS = $(shell find lib -type f -name "*.coda" 2>/dev/null)
STDLIB_ASM_SRCS  = $(shell find lib -type f -name "*.s" 2>/dev/null)

STDLIB_GEN_ASM   = $(patsubst lib/%.coda,build/lib/%.s,$(STDLIB_CODA_SRCS))
STDLIB_COP_ASM   = $(patsubst lib/%.s,build/lib/%.s,$(STDLIB_ASM_SRCS))

STDLIB_OBJS      = $(patsubst build/lib/%.s,build/lib/%.o,$(STDLIB_GEN_ASM) $(STDLIB_COP_ASM))

STDLIB_TARGET_DIR = build/$(HOST_ARCH)/$(HOST_OS)/lib
STDLIB_SO         = $(STDLIB_TARGET_DIR)/libcoda.$(SOEXT)

.PHONY: all clean backends stdlib
.SECONDARY: $(STDLIB_GEN_ASM)

all: $(TARGET) backends stdlib

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -rdynamic

build/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/backends/%.o: $(SRC)/backends/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -c -o $@ $<

$(foreach d,$(BACKEND_DIRS), \
  $(eval BACKEND_SRCS_$(d) := $(wildcard $(SRC)/backends/$(d)/*.c)) \
  $(eval BACKEND_OBJS_$(d) := $(patsubst $(SRC)/%.c,build/%.o,$(BACKEND_SRCS_$(d)))) \
  $(eval build/backends/$(d).$(SOEXT): $$(BACKEND_OBJS_$(d))) \
  $(eval build/backends/$(d).$(SOEXT): ; \
    @mkdir -p build/backends/$(d) ; \
    if [ -n "$$^" ]; then \
        echo "$(CC) $(DLL_LINK) -o $$@ $$^"; \
        $(CC) $(DLL_LINK) -o $$@ $$^; \
    fi ) \
)

backends: $(BACKENDS_SO)
	@for b in $(BACKEND_DIRS); do \
		if [ -f build/backends/$$b.$(SOEXT) ]; then \
			mkdir -p build/$$b; \
			cp build/backends/$$b.$(SOEXT) build/$$b/backend.$(SOEXT); \
		fi \
	done

stdlib: $(STDLIB_SO)

$(STDLIB_SO): $(STDLIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(DLL_LINK) -nostdlib -o $@ $^

build/lib/%.o: build/lib/%.s
	@mkdir -p $(dir $@)
	$(CC) -nostdlib -c -o $@ $<

build/lib/%.s: lib/%.coda $(TARGET) backends
	@mkdir -p $(dir $@)
	./$(TARGET) -o $@ $<

build/lib/%.s: lib/%.s
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	@rm -rf build
	@rm -f $(TARGET)

.PHONY: locs
locs:
	cloc --read-lang-def=coda.lang --vcs=git .

.PHONY: tree
tree:
	git ls-files | tree --fromfile

-include $(patsubst %.o,%.d,$(OBJS) $(BACKEND_OBJS))