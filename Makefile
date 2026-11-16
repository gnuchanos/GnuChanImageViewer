.PHONY: all build test clean install

BUILD_DIR ?= build
CXX ?= g++
CPPFLAGS += -Isrc $(shell pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0 libwebp)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0 libwebp)
PREFIX ?= /usr/local

all: build

build: $(BUILD_DIR)/GnuChanImageViewer

$(BUILD_DIR)/GnuChanImageViewer: src/main.cpp src/image_core.cpp src/image_core.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/image_core.cpp -o $@ $(LDLIBS)

$(BUILD_DIR)/image_core_tests: tests/test_image_core.cpp src/image_core.cpp src/image_core.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_image_core.cpp src/image_core.cpp -o $@ $(LDLIBS)

test: $(BUILD_DIR)/GnuChanImageViewer $(BUILD_DIR)/image_core_tests
	$(BUILD_DIR)/image_core_tests
	mkdir -p $(BUILD_DIR)/test-config/GnuChanImageViewer
	rm -f $(BUILD_DIR)/test-config/GnuChanImageViewer/config.conf
	test "$$(XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/test-config) $(BUILD_DIR)/GnuChanImageViewer --check-config)" = "Config OK: 1100 x 760"
	printf '[General]\nWindowWidth=1280\nWindowHeight=900\n' > $(BUILD_DIR)/test-config/GnuChanImageViewer/config.conf
	test "$$(XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/test-config) $(BUILD_DIR)/GnuChanImageViewer --check-config)" = "Config OK: 1280 x 900"
	printf '[General]\nWindowWidth=400\nWindowHeight=900\n' > $(BUILD_DIR)/test-config/GnuChanImageViewer/config.conf
	! XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/test-config) $(BUILD_DIR)/GnuChanImageViewer --check-config
	printf '[General]\nWindowWidth=1100\nWindowHeight=760\n' > $(BUILD_DIR)/test-config/GnuChanImageViewer/config.conf
	@if command -v xvfb-run >/dev/null 2>&1 && [ -x "$$(command -v xvfb-run)" ]; then \
		XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/test-config) xvfb-run -a $(BUILD_DIR)/GnuChanImageViewer --smoke-test; \
	elif [ -n "$$DISPLAY" ]; then \
		XDG_CONFIG_HOME=$(abspath $(BUILD_DIR)/test-config) $(BUILD_DIR)/GnuChanImageViewer --smoke-test; \
	else \
		echo "error: xvfb-run or an active DISPLAY is required for the GUI smoke test" >&2; exit 1; \
	fi

install: build
	install -Dm755 $(BUILD_DIR)/GnuChanImageViewer $(DESTDIR)$(PREFIX)/bin/GnuChanImageViewer
	install -Dm644 data/gnuchan-image-viewer.desktop $(DESTDIR)$(PREFIX)/share/applications/gnuchan-image-viewer.desktop
	install -Dm644 src/config/config.conf $(DESTDIR)$(PREFIX)/share/gnuchan-image-viewer/config.conf

clean:
	rm -rf $(BUILD_DIR)
