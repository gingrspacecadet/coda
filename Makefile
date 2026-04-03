CXX = g++
CXXFLAGS = -g -Werror -MMD -MD -std=c++23 -include $(PCH)

SRCS = $(shell find src/ -type f -name "*.cpp" 2>/dev/null)
OBJS = $(patsubst %.cpp,%.o,$(SRCS))

PCH = src/pch.hpp
PCH_GCH = $(PCH).gch

TARGET = coda

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $@ $^

$(PCH_GCH): $(PCH)
	$(CXX) $(CXXFLAGS) -x c++-header -o $@ $<

%.o: %.cpp | $(PCH_GCH)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@find -type f -regex '.*\.\(o\|d\|gch\)' -delete
	@rm -f $(TARGET)

-include $(patsubst %.o,%.d,$(OBJS))