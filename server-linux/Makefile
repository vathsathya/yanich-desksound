CXX = g++
APPIND_CFLAGS = $(shell pkg-config --cflags ayatana-appindicator3-0.1 2>/dev/null || pkg-config --cflags appindicator3-0.1 2>/dev/null)
APPIND_LIBS = $(shell pkg-config --libs ayatana-appindicator3-0.1 2>/dev/null || pkg-config --libs appindicator3-0.1 2>/dev/null)

GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 2>/dev/null) $(APPIND_CFLAGS)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0 2>/dev/null) $(APPIND_LIBS)

CXXFLAGS = -O3 -std=c++17 -Wall -Iinclude $(GTK_CFLAGS)
LIBS = -lpulse-simple -lpulse -lpthread $(GTK_LIBS)

SRCS = src/main_linux.cpp src/audio_pulse.cpp src/config_manager.cpp src/logger.cpp src/tray_linux.cpp src/gui_linux.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = build/desksound-linux

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) $(LIBS) -o $(TARGET)

clean:
	rm -rf build src/*.o

.PHONY: all clean
