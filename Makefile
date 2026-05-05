# jt-synth — PocketDAW plugin
# Builds: Linux .so, Windows .dll (MinGW cross), aarch64 .so (RG35XX)

POCKETDAW_DIR ?= ../pocket-daw
DEPLOY_DIR    := $(POCKETDAW_DIR)/plugins/synths/jt-synth

SDK_DIR       := sdk
SRC           := src/jt-synth.c
BUILD_DIR     := build
NAME          := jt-synth

GCC           ?= gcc
WINCC         ?= x86_64-w64-mingw32-gcc
ARMCC         ?= aarch64-none-linux-gnu-gcc

SDL2_WIN      ?= /tmp/SDL2-2.30.10/x86_64-w64-mingw32
SDL2_LINUX_CF := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")

.PHONY: all linux windows device deploy clean

all: linux

linux:   $(BUILD_DIR)/$(NAME).so
windows: $(BUILD_DIR)/$(NAME).dll
device:  $(BUILD_DIR)/$(NAME)-arm64.so

$(BUILD_DIR)/$(NAME).so: $(SRC) | $(BUILD_DIR)
	$(GCC) -shared -fPIC -O2 -I$(SDK_DIR) $(SDL2_LINUX_CF) -o $@ $< -lm

$(BUILD_DIR)/$(NAME).dll: $(SRC) | $(BUILD_DIR)
	$(WINCC) -shared -O2 -I$(SDK_DIR) -I$(SDL2_WIN)/include -L$(SDL2_WIN)/lib \
		-Wl,--export-all-symbols -o $@ $< -lSDL2 -lm

$(BUILD_DIR)/$(NAME)-arm64.so: $(SRC) | $(BUILD_DIR)
	$(ARMCC) -shared -fPIC -O2 -I$(SDK_DIR) -o $@ $< -lm

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

deploy: all
	mkdir -p $(DEPLOY_DIR)
	cp $(BUILD_DIR)/$(NAME).so  $(DEPLOY_DIR)/$(NAME).so  2>/dev/null || true
	cp $(BUILD_DIR)/$(NAME).dll $(DEPLOY_DIR)/$(NAME).dll 2>/dev/null || true
	cp manifest.json $(DEPLOY_DIR)/manifest.json

clean:
	rm -rf $(BUILD_DIR)
