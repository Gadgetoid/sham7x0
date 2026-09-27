PROG      = sham7x0

.DEFAULT_GOAL := $(PROG)
IMGUI     = lib/imgui
Z80       = lib/z80
BUILD     = build
ROM      ?= rom/r162.da1

CFLAGS  += -I. -Isrc -Ilib -I$(Z80) -I$(IMGUI) -I$(IMGUI)/backends
CFLAGS  += -Wall -O2 -fno-common -MMD -MP
CFLAGS  += $(shell pkg-config --cflags sdl3)

LDFLAGS += $(shell pkg-config --libs sdl3) -framework CoreServices -framework Cocoa

CXXFLAGS = $(filter-out -std=c99,$(CFLAGS)) -std=c++17

SRC_MACHINE = src/machine.c src/wzd.c src/serial.c src/pclink.c $(Z80)/z80.c

SRC_APP = \
	$(SRC_MACHINE) \
	src/runtime.c \
	src/keys.c \
	src/lcd.c \
	src/beeper.c

TOUCHSCREEN ?= 0
ifeq ($(TOUCHSCREEN),1)
BUILD   := $(BUILD)/touchscreen
CFLAGS  += -DSHAM_TOUCHSCREEN
LDFLAGS += -framework IOKit
SRC_APP += src/touch_macos.c
endif

CONFIG = build/config
$(shell mkdir -p build; echo "TOUCHSCREEN=$(TOUCHSCREEN)" | cmp -s - $(CONFIG) || { echo "TOUCHSCREEN=$(TOUCHSCREEN)" > $(CONFIG); rm -f $(PROG); })

SRC_OBJC = src/menu_macos.m

SRC_APP_CXX = \
	src/main.cpp \
	src/device.cpp \
	src/case_raster.cpp \
	src/browser.cpp \
	src/firmware.cpp \
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

headless: tools/headless.c $(SRC_MACHINE) src/lcd.c src/machine.h src/wzd.h src/serial.h src/pclink.h src/lcd.h
	$(CC) -Wall -O2 -fno-common -Isrc -I$(Z80) -o $@ tools/headless.c $(SRC_MACHINE) src/lcd.c

-include $(DEPS)

run: $(PROG)
	./$(PROG) --rom=$(ROM)

screenshot: $(PROG)
	./$(PROG) --rom=$(ROM) --screenshot=$(BUILD)/screenshot.bmp

test: headless
	python3 tools/test.py

keyboard:
	python3 tools/make_keyboard.py tools/keyboard_layout.json src/keyboard_layout.h tools/zq770_layout.svg
	python3 tools/make_lid.py tools/zq770_layout.svg src/lid_layout.h

clean:
	rm -rf $(BUILD) $(PROG) headless

.PHONY: run screenshot test keyboard clean
