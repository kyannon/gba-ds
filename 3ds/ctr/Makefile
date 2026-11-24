# GBA DS for the New Nintendo 3DS: the app (../../src) on armGBA (../emu) with the 3DS host in this
# folder (sdl_ctr.c stands in for SDL2), packed into gba-ds.3dsx. `make 3ds` in ../ runs this in the
# devkitpro/devkitarm image (devkitARM with libctru, citro3d, citro2d and zlib).
#
#   make             gba-ds.3dsx
#   make TEST=1      gba-ds-test.3dsx, for ../test (the Azahar emulator)
#   make LOG_URL=http://<host>:<port>/log   a diagnosis build: its logs sent there (../tools/logserver.py) when the
#                    app starts (the last run's) and closes. Never a release; rebuild host.c without it after.
#   make cia-parts   what bannertool makes for the .cia (../Makefile's `cia` then runs makerom: cia.sh)

ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM in the environment: export DEVKITARM=<devkitPro>/devkitARM")
endif
include $(DEVKITARM)/3ds_rules
.DEFAULT_GOAL := all

REPO    := $(abspath ../..)
# TEST=1: gba-ds-test.3dsx, with the capture test's profile (src/profiles.c, GBADS_TEST_PROFILE). Never a player's.
comma := ,
TARGET  := gba-ds$(if $(TEST),-test)
TITLE   := GBA DS$(if $(TEST), test)
# The 3DS port's own version (../VERSION); UPSTREAM is the GBA DS it is.
VERSION := $(shell cat $(REPO)/3ds/VERSION)
UPSTREAM := $(shell cat $(REPO)/VERSION)
BUILD   := build$(if $(TEST),-test)

ARMGBA := $(REPO)/third_party/armgba
ARMGBA_BACKEND := a32
include $(REPO)/3ds/emu/armgba.mk

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
# As armGBA-3DS builds (its Makefile says why): -fwrapv or every game hangs after 128 seconds,
# -flto because the core's hot paths cross its files.
COMMON   := -g -fwrapv -flto -fomit-frame-pointer -ffunction-sections -fdata-sections -mword-relocations $(ARCH) \
            -D__3DS__ -I$(CTRULIB)/include -I$(PORTLIBS)/include
CFLAGS   := $(COMMON) -std=gnu11
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g -O3 -fwrapv -flto=auto -mword-relocations $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections -Wl,--wrap=stat$(if $(TEST), -Wl$(comma)--wrap=malloc -Wl$(comma)--wrap=calloc)
LIBS     := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib -lcitro2d -lcitro3d -lctru -lz -lm

D3 := $(REPO)/3ds
S  := $(REPO)/src
APP_SRC := $(addprefix $(S)/,main.c config.c i18n.c i18n_data.c font.c ui.c library.c mapper.c hotkeys.c game.c paths.c image.c brand_data.c \
	bezel.c bezel_logo.c logo.c cheats.c folderpick.c textfont.c pixfont.c ra.c sha256.c patch.c mapcap.c profiles.c logo_masks.c) \
	$(D3)/emu/emu_armgba.c $(D3)/ctr/stubs.c $(D3)/ctr/fonts_ctr.c
# WebP logos: libwebp's decoder, as ../../Makefile builds it
WEBP_DIR := $(REPO)/third_party/libwebp
WEBP_SRC := $(wildcard $(WEBP_DIR)/src/dec/*.c $(WEBP_DIR)/src/dsp/*.c $(WEBP_DIR)/src/utils/*.c)
HOST_SRC := $(D3)/ctr/host.c $(D3)/ctr/sdl_ctr.c $(D3)/ctr/jitmem.c $(ARMGBA)/platform/3ds/source/svc.s

obj = $(patsubst $(REPO)/%,$(BUILD)/%.o,$(1))
APP_OBJ  := $(call obj,$(APP_SRC))
HOST_OBJ := $(call obj,$(HOST_SRC))
CORE_OBJ := $(call obj,$(ARMGBA_CORE_SRC))
WEBP_OBJ := $(call obj,$(WEBP_SRC))
OBJ := $(APP_OBJ) $(HOST_OBJ) $(CORE_OBJ) $(WEBP_OBJ)

# sdl/ stands where SDL2's headers do. armGBA's src first: its cheats.h is the one emu_armgba.c
# means. The app's threads are made by host.c (the core they run on), and its main is called from
# host.c's.
APP_INC := -I$(D3)/ctr/sdl -I$(D3)/ctr/compat -I$(D3)/ctr -I$(ARMGBA)/src -I$(S) -I$(WEBP_DIR)
$(APP_OBJ):  EXTRA := -O2 -Wall -Wno-unused-parameter $(APP_INC) -DGBADS_ARMGBA -DGBADS_VERSION='"$(VERSION)"' \
                      -Dpthread_create=host_pthread_create -Dmain=gbads_main -D_GNU_SOURCE $(if $(TEST),-DGBADS_TEST_PROFILE)
$(HOST_OBJ): EXTRA := -O2 -Wall -Wextra $(APP_INC) -DGBADS_VERSION='"$(VERSION)"' -DGBADS_UPSTREAM='"$(UPSTREAM)"' $(if $(LOG_URL),-DGBADS_LOG_URL='"$(LOG_URL)"')
$(CORE_OBJ): EXTRA := $(ARMGBA_CORE_FLAGS)
$(WEBP_OBJ): EXTRA := -O2 -I$(WEBP_DIR)

.PHONY: all cia-parts clean
all: $(TARGET).3dsx

$(BUILD)/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) $(EXTRA) -c $< -o $@

$(BUILD)/%.s.o: $(REPO)/%.s
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	@echo linking $@
	@$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: icon.png
	@smdhtool --create "$(TITLE)" "GBA games with their own map on the second screen" "GBA DS" icon.png $@

$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(BUILD)/$(TARGET).smdh
	@echo built $@

# The CIA: makerom packs the ELF with the permissions the recompiler needs: armGBA-3DS's own
# (platform/3ds/armgba.rsf), with GBA DS's title, product code and unique ID in place of armGBA's.
BANNERTOOL ?= bannertool
$(BUILD)/banner.bnr: banner.png banner.wav
	@mkdir -p $(BUILD)
	@$(BANNERTOOL) makebanner -i banner.png -a banner.wav -o $@
$(BUILD)/icon.icn: icon.png
	@$(BANNERTOOL) makesmdh -s "$(TITLE)" -l "GBA games with their own map on the second screen" -p "GBA DS" -i icon.png -o $@
$(BUILD)/gba-ds.rsf: $(ARMGBA)/platform/3ds/armgba.rsf
	@sed -e 's/: armGBA$$/: GBA DS/' -e 's/CTR-H-AGBA/CTR-H-GBDS/' -e 's/0xFA6BA/0xFD5D5/' $< > $@
cia-parts: $(BUILD)/$(TARGET).elf $(BUILD)/banner.bnr $(BUILD)/icon.icn $(BUILD)/gba-ds.rsf

clean:
	rm -rf build build-test gba-ds.3dsx gba-ds-test.3dsx gba-ds.cia

-include $(OBJ:.o=.d)
