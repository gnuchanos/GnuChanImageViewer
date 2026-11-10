.PHONY: all build test clean install

BUILD_DIR ?= build
CXX ?= g++
CPPFLAGS += -Isrc $(shell pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0 libwebp)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0 libwebp)
PREFIX ?= /usr/local

all: build

build: $(BUILD_DIR)/gnuchan-image-viewer

$(BUILD_DIR)/gnuchan-image-viewer: src/main.cpp src/image_core.cpp src/image_core.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/image_core.cpp -o $@ $(LDLIBS)

$(BUILD_DIR)/image_core_tests: tests/test_image_core.cpp src/image_core.cpp src/image_core.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_image_core.cpp src/image_core.cpp -o $@ $(LDLIBS)

test: $(BUILD_DIR)/gnuchan-image-viewer $(BUILD_DIR)/image_core_tests
	$(BUILD_DIR)/image_core_tests
	xvfb-run -a $(BUILD_DIR)/gnuchan-image-viewer --smoke-test

install: build
	install -Dm755 $(BUILD_DIR)/gnuchan-image-viewer $(DESTDIR)$(PREFIX)/bin/gnuchan-image-viewer
	install -Dm644 data/gnuchan-image-viewer.desktop $(DESTDIR)$(PREFIX)/share/applications/gnuchan-image-viewer.desktop

clean:
	rm -rf $(BUILD_DIR)
