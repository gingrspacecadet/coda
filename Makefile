CC = gcc
CFLAGS = -g -Werror -MMD -MD -std=gnu23

SRCS = $(filter-out src/backends/%,$(shell find src/ -type f -name "*.c" 2>/dev/null))
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))

TARGET = coda

BACKEND_DIRS = $(shell find src/backends -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null)
BACKEND_SRCS = $(shell find src/backends -type f -name "*.c")
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

backends: $(BACKENDS_SO)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -rdynamic

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/backends/%.$(SOEXT):
	@mkdir -p $(dir $@)
	@objs="$(patsubst src/backends/$*/*.c,build/backends/$*/*.o,$(wildcard src/backends/$*/*.c))"; \
	if [ -z "$$objs" ]; then \
		echo "No sources for backend '$*', skipping"; \
	else \
		echo "Linking $@ from $$objs"; \
		$(CC) $(DLL_LINK) -o $@ $$objs; \
	fi

build/backends/%.o: src/backends/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -c -o $@ $<

clean:
	@rm -rf build
	@rm -f $(TARGET)

-include $(patsubst %.o,%.d,$(OBJS)) $(patsubst %.o,%.d,$(BACKEND_OBJS))