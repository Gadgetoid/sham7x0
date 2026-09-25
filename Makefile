PROG      = zq77x-emu

.DEFAULT_GOAL := $(PROG)
IMGUI     = lib/imgui
Z80       = lib/z80
BUILD     = build
ROM      ?= rom/r162.da1

CFLAGS  += -I. -Isrc -Ilib -I$(Z80) -I$(IMGUI) -I$(IMGUI)/backends
CFLAGS  += -Wall -O2 -fno-common -MMD -MP
CFLAGS  += $(shell pkg-config --cflags sdl3)

LDFLAGS += $(shell pkg-config --libs sdl3) -framework CoreServices -framework Cocoa -framework IOKit

CXXFLAGS = $(filter-out -std=c99,$(CFLAGS)) -std=c++17

SRC_MACHINE = src/machine.c src/wzd.c $(Z80)/z80.c

SRC_APP = \
	$(SRC_MACHINE) \
	src/runtime.c \
	src/keys.c \
	src/lcd.c \
	src/beeper.c \
	src/touch_macos.c

SRC_OBJC = src/menu_macos.m

SRC_APP_CXX = \
	src/main.cpp \
	src/device.cpp \
	src/console.cpp

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

headless: tools/headless.c $(SRC_MACHINE) src/machine.h src/wzd.h
	$(CC) -Wall -O2 -Isrc -I$(Z80) -o $@ tools/headless.c $(SRC_MACHINE)

-include $(DEPS)

run: $(PROG)
	./$(PROG) --rom=$(ROM)

screenshot: $(PROG)
	./$(PROG) --rom=$(ROM) --screenshot=$(BUILD)/screenshot.bmp

test: headless
	python3 tools/test.py

keyboard:
	python3 tools/make_keyboard.py tools/keyboard_layout.json src/keyboard_layout.h

clean:
	rm -rf $(BUILD) $(PROG) headless

.PHONY: run screenshot test keyboard clean
