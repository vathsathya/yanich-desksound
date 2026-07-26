CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Iinclude
LIBS = -lpulse-simple -lpulse -lpthread

SRCS = src/main_linux.cpp src/audio_pulse.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = build/desksound-linux

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) $(LIBS) -o $(TARGET)

clean:
	rm -rf build src/*.o

.PHONY: all clean
