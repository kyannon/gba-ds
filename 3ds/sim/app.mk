# The whole app on armGBA for the desktop: src/ as the host build has it, with emu/emu_armgba.c and armGBA's
# x86-64 recompiler in place of libmgba, on the real SDL2 (deps/sdl-host). Runs headless with the same input
# scripts as build/host/gba-ds (CLAUDE.md, "Testing without the device"), so a script's pictures can be compared
# between the two emulators.
#   make -f app.mk                 build/sim/gba-ds
#   make -f app.mk ARMGBA=<tree>   another armGBA (inside the repo, or linked there)
REPO := $(abspath ../..)
OUT ?= $(REPO)/build/sim
ARMGBA ?= $(REPO)/third_party/armgba
ARMGBA_BACKEND := x64
include $(REPO)/3ds/emu/armgba.mk
SDL_PREFIX ?= $(REPO)/deps/sdl-host

CC ?= cc
S := $(REPO)/src
APP_SRC := $(addprefix $(S)/,main.c config.c font.c ui.c library.c mapper.c hotkeys.c game.c paths.c image.c brand_data.c \
	bezel.c bezel_logo.c logo.c cheats.c esinteg.c clocks.c textfont.c fonts_data.c ra.c sha256.c patch.c mapcap.c \
	profiles.c logo_masks.c) $(REPO)/3ds/emu/emu_armgba.c
# WebP logos: libwebp's decoder, as ../../Makefile builds it
WEBP_DIR := $(REPO)/third_party/libwebp
WEBP_SRC := $(wildcard $(WEBP_DIR)/src/dec/*.c $(WEBP_DIR)/src/dsp/*.c $(WEBP_DIR)/src/utils/*.c)
obj = $(patsubst $(REPO)/%,$(OUT)/app-obj/%.o,$(1))
APP_OBJ := $(call obj,$(APP_SRC))
WEBP_OBJ := $(call obj,$(WEBP_SRC))
CORE_OBJ := $(call obj,$(ARMGBA_CORE_SRC))
VERSION := $(shell cat $(REPO)/VERSION)
$(APP_OBJ):  EXTRA := -std=gnu11 -O2 -g -Wall -Wno-unused-parameter -Wno-missing-field-initializers -DGBADS_ARMGBA \
	-DGBADS_VERSION='"$(VERSION)+armgba"' -I$(ARMGBA)/src -I$(S) -I$(WEBP_DIR) -I$(SDL_PREFIX)/include/SDL2 -D_REENTRANT -D_GNU_SOURCE
$(CORE_OBJ): EXTRA := -std=gnu11 $(ARMGBA_CORE_FLAGS)
$(WEBP_OBJ): EXTRA := -std=gnu11 -O2 -I$(WEBP_DIR)

$(OUT)/gba-ds: $(APP_OBJ) $(CORE_OBJ) $(WEBP_OBJ)
	@echo linking $@
	@$(CC) -g $^ -L$(SDL_PREFIX)/lib -Wl,-rpath,$(SDL_PREFIX)/lib -lSDL2 -lz -lm -lpthread -ldl -o $@

$(OUT)/app-obj/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(EXTRA) -c $< -o $@

-include $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d) $(WEBP_OBJ:.o=.d)
