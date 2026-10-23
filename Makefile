# GBA DS build
#
#   scripts/setup-host.sh     first time: fetches and builds mGBA and SDL2 into deps/, then builds
#   make / make tools         host build (build/host/gba-ds, build/host/harness)
#   make CROSS=aarch64-linux-gnu- MGBA_BUILD=../mgba/build-aarch64 SDL_PREFIX=../sdl-aarch64
#
# MGBA_SRC   mGBA source tree (headers)
# MGBA_BUILD a cmake build dir of mGBA (see scripts/build-rocknix.sh for the options), targets mgba and zlibstatic built
# SDL_PREFIX an SDL2 install prefix (include/SDL2, lib/libSDL2.so)

CROSS      ?=
CC         := $(CROSS)gcc
MGBA_SRC   ?= deps/mgba
MGBA_BUILD ?= $(MGBA_SRC)/build-host
SDL_PREFIX ?= deps/sdl-host
OUT        ?= build/$(if $(CROSS),$(patsubst %-,%,$(CROSS)),host)

MGBA_DEFS  := $(shell sed -n 's/^C_DEFINES = //p' $(MGBA_BUILD)/CMakeFiles/mgba.dir/flags.make)
VERSION    := $(shell cat VERSION 2>/dev/null || echo dev)
OPTFLAGS   ?= -O2 -g
override CFLAGS := $(OPTFLAGS) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
              -DGBADS_VERSION='"$(VERSION)"' $(MGBA_DEFS) -I$(MGBA_SRC)/include -I$(MGBA_BUILD)/include -I$(SDL_PREFIX)/include/SDL2 -D_REENTRANT
LDFLAGS    += -L$(SDL_PREFIX)/lib -Wl,-rpath-link,$(SDL_PREFIX)/lib
# mGBA's bundled zlib (for .zip ROMs), when it was built with it
ZLIB_A     := $(wildcard $(MGBA_BUILD)/zlib/libz.a)
LIBS       := $(MGBA_BUILD)/libmgba.a $(ZLIB_A) -lSDL2 -lpthread -lm

CORE_SRC   := src/emu.c src/mapcap.c src/profiles.c src/logo_masks.c
# RetroAchievements: rcheevos (MIT), vendored in third_party/rcheevos
RC_DIR     := third_party/rcheevos
RC_SRC     := $(wildcard $(RC_DIR)/src/*.c $(RC_DIR)/src/rapi/*.c $(RC_DIR)/src/rcheevos/*.c) $(RC_DIR)/src/rhash/md5.c
RC_OBJ     := $(patsubst $(RC_DIR)/src/%.c,$(OUT)/rcheevos/%.o,$(RC_SRC))

APP_SRC    := src/main.c src/config.c src/font.c src/ui.c src/library.c src/mapper.c src/game.c src/paths.c src/image.c src/brand_data.c src/logo.c src/mapframe.c src/cheats.c src/esinteg.c src/textfont.c src/fonts_data.c src/ra.c $(CORE_SRC)

all: $(OUT)/gba-ds

$(OUT)/gba-ds: $(APP_SRC) src/*.h VERSION $(MGBA_BUILD)/libmgba.a $(RC_OBJ)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DGBADS_RA -I$(RC_DIR)/include -I$(RC_DIR)/src -o $@ $(APP_SRC) $(RC_OBJ) $(LDFLAGS) $(LIBS)

$(OUT)/rcheevos/%.o: $(RC_DIR)/src/%.c $(wildcard $(RC_DIR)/include/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(OPTFLAGS) -std=gnu11 -I$(RC_DIR)/include -c $< -o $@

# development tools (host only; need libpng)
tools: $(OUT)/harness

$(OUT)/harness: tools/harness.c tools/png.c $(CORE_SRC) src/logo.c src/*.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tools/harness.c tools/png.c $(CORE_SRC) src/logo.c $(MGBA_BUILD)/libmgba.a $(ZLIB_A) -lpng -lpthread -lm

clean:
	rm -rf build

.PHONY: all tools clean
