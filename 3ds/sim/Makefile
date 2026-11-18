# The desktop test build: GBA DS's harness (tools/harness.c) and its game code (map captures, profiles, logos) on
# armGBA with its x86-64 recompiler, in place of libmgba. What the 3DS build runs, without a console.
#   make            build/sim/harness
REPO := $(abspath ../..)
OUT ?= $(REPO)/build/sim
ARMGBA := $(REPO)/third_party/armgba
ARMGBA_BACKEND := x64
include $(REPO)/3ds/emu/armgba.mk

CC ?= cc
APP_SRC := $(REPO)/tools/harness.c $(REPO)/3ds/sim/png.c $(REPO)/src/patch.c $(REPO)/src/mapcap.c $(REPO)/src/profiles.c \
	$(REPO)/src/logo_masks.c $(REPO)/src/logo.c $(REPO)/3ds/emu/emu_armgba.c
obj = $(patsubst $(REPO)/%,$(OUT)/obj/%.o,$(1))
APP_OBJ := $(call obj,$(APP_SRC))
CORE_OBJ := $(call obj,$(ARMGBA_CORE_SRC))
$(APP_OBJ):  EXTRA := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DGBADS_ARMGBA \
	-DGBADS_VERSION='"sim"' -DGBADS_TEST_PROFILE -I$(ARMGBA)/src -I$(REPO)/src -D_REENTRANT
$(CORE_OBJ): EXTRA := -std=gnu11 $(ARMGBA_CORE_FLAGS)

$(OUT)/harness: $(APP_OBJ) $(CORE_OBJ)
	@echo linking $@
	@$(CC) -g $^ -lz -lm -lpthread -o $@

$(OUT)/obj/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(EXTRA) -c $< -o $@

-include $(APP_OBJ:.o=.d) $(CORE_OBJ:.o=.d)
