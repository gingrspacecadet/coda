CXX = g++
CXXFLAGS = -g -Werror -MMD -MD -std=c++23

SRCS = $(shell find src/ -type f -name "*.cpp" 2>/dev/null)
OBJS = $(patsubst %.cpp,%.o,$(SRCS))

TARGET = coda

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@find -type f -name *.o -delete
	@rm -f $(TARGET)

-include $(patsubst %.o,%.d,$(OBJS))