ifeq ($(OS),Windows_NT)
EXE       = .exe
endif
PROG      = sham7x0$(EXE)
HEADLESS  = headless$(EXE)

.DEFAULT_GOAL := $(PROG)
IMGUI     = lib/imgui
Z80       = lib/z80
BUILD     = build
ROM      ?= rom/r162.da1

CFLAGS  += -I. -Isrc -Ilib -I$(Z80) -I$(IMGUI) -I$(IMGUI)/backends
CFLAGS  += -Wall -O2 -fno-common -MMD -MP
CFLAGS  += $(shell pkg-config --cflags sdl3)

LDFLAGS += $(shell pkg-config --libs sdl3)

UNAME := $(shell uname -s)
ifeq ($(OS),Windows_NT)
SRC_OBJC    =
SRC_MENU    = src/menu_imgui.cpp
SRC_SERIAL  = src/serial_win32.c
SYSTEM_LIBS = -lpthread
else ifeq ($(UNAME),Darwin)
LDFLAGS    += -framework CoreServices -framework Cocoa
SRC_OBJC    = src/menu_macos.m
SRC_MENU    =
SRC_SERIAL  = src/serial_posix.c
SYSTEM_LIBS =
else
SRC_OBJC    =
SRC_MENU    = src/menu_imgui.cpp
SRC_SERIAL  = src/serial_posix.c
SYSTEM_LIBS = -lutil -lm -lpthread
endif
LDFLAGS += $(SYSTEM_LIBS)

CXXFLAGS = $(filter-out -std=c99,$(CFLAGS)) -std=c++17

SRC_MACHINE = src/machine.c src/wzd.c src/serial.c $(SRC_SERIAL) src/pclink.c $(Z80)/z80.c

SRC_APP = \
	$(SRC_MACHINE) \
	src/runtime.c \
	src/keys.c \
	src/lcd.c \
	src/beeper.c \
	src/sha256.c

TOUCHSCREEN ?= 0
ifeq ($(TOUCHSCREEN),1)
ifneq ($(UNAME),Darwin)
$(error TOUCHSCREEN=1 is macOS only)
endif
BUILD   := $(BUILD)/touchscreen
CFLAGS  += -DSHAM_TOUCHSCREEN
LDFLAGS += -framework IOKit
SRC_APP += src/touch_macos.c
endif

CONFIG      = build/config
CONFIG_TEXT = TOUCHSCREEN=$(TOUCHSCREEN)
ifneq ($(CONFIG_TEXT),$(shell cat $(CONFIG) 2>/dev/null))
$(shell mkdir -p build; echo "$(CONFIG_TEXT)" > $(CONFIG); rm -f $(PROG))
endif

SRC_APP_CXX = \
	src/main.cpp \
	src/device.cpp \
	src/case_raster.cpp \
	src/browser.cpp \
	src/firmware.cpp \
	src/console.cpp \
	$(SRC_MENU)

SRC_IMGUI = $(addprefix $(IMGUI)/, \
	imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
	backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlrenderer3.cpp)

OBJ  = $(addprefix $(BUILD)/,$(SRC_APP:.c=.o) $(SRC_APP_CXX:.cpp=.opp) $(SRC_IMGUI:.cpp=.opp) $(SRC_OBJC:.m=.om))
DEPS = $(OBJ:.o=.d)
DEPS := $(DEPS:.opp=.d)
DEPS := $(DEPS:.om=.d)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.opp: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.om: %.m
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fobjc-arc -c -o $@ $<

$(PROG): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(HEADLESS): tools/headless.c $(SRC_MACHINE) src/lcd.c src/machine.h src/wzd.h src/serial.h src/serial_port.h src/pclink.h src/lcd.h
	$(CC) -Wall -O2 -fno-common -Isrc -I$(Z80) -o $@ tools/headless.c $(SRC_MACHINE) src/lcd.c $(SYSTEM_LIBS)

-include $(DEPS)

ARCH        := $(shell uname -m)
SDL_PREFIX  := $(shell pkg-config --variable=prefix sdl3)
SDL_LICENCE := $(firstword $(wildcard $(SDL_PREFIX)/share/licenses/SDL3/LICENSE.txt $(SDL_PREFIX)/share/licenses/sdl3/LICENSE.txt))
ifeq ($(OS),Windows_NT)
DIST_OS     = windows
else ifeq ($(UNAME),Darwin)
DIST_OS     = macos
else
DIST_OS     = linux
endif
DIST_NAME   = sham7x0-$(DIST_OS)-$(ARCH)
DIST_DIR    = dist/$(DIST_NAME)

dist: $(PROG) $(HEADLESS)
	rm -rf $(DIST_DIR) dist/$(DIST_NAME).zip
	mkdir -p $(DIST_DIR)/licences
	cp $(PROG) $(HEADLESS) README.md $(DIST_DIR)/
	cp -R assets $(DIST_DIR)/
	cp licences/* $(DIST_DIR)/licences/
	cp lib/z80/LICENSE $(DIST_DIR)/licences/z80.txt
	cp lib/imgui/LICENSE.txt $(DIST_DIR)/licences/imgui.txt
ifeq ($(DIST_OS),macos)
	cp $(shell pkg-config --variable=libdir sdl3)/libSDL3.0.dylib $(DIST_DIR)/
	install_name_tool -id @executable_path/libSDL3.0.dylib $(DIST_DIR)/libSDL3.0.dylib
	install_name_tool -change "$$(otool -L $(PROG) | awk '/libSDL3/ { print $$1 }')" @executable_path/libSDL3.0.dylib $(DIST_DIR)/$(PROG)
	codesign --force --sign - $(DIST_DIR)/libSDL3.0.dylib $(DIST_DIR)/$(PROG)
endif
ifeq ($(DIST_OS),windows)
	cp $$(ldd $(PROG) $(HEADLESS) | awk '$$3 ~ /^\/(ucrt64|mingw64|clang64)\// { print $$3 }' | sort -u) $(DIST_DIR)/
endif
ifneq ($(DIST_OS),linux)
ifneq ($(SDL_LICENCE),)
	cp $(SDL_LICENCE) $(DIST_DIR)/licences/SDL3.txt
endif
endif
	cd dist && zip -qry $(DIST_NAME).zip $(DIST_NAME)

run: $(PROG)
	./$(PROG) --rom=$(ROM)

screenshot: $(PROG)
	./$(PROG) --rom=$(ROM) --screenshot=$(BUILD)/screenshot.bmp

ifneq ($(EXE),)
headless: $(HEADLESS)
.PHONY: headless
endif

test: $(HEADLESS)
	python3 tools/test.py

keyboard:
	python3 tools/make_keyboard.py tools/keyboard_layout.json src/keyboard_layout.h tools/zq770_layout.svg
	python3 tools/make_lid.py tools/zq770_layout.svg src/lid_layout.h

clean:
	rm -rf build dist $(PROG) $(HEADLESS)

.PHONY: run screenshot test keyboard dist clean
