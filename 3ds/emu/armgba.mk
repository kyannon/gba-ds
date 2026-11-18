# armGBA's core (third_party/armgba, a submodule), as its own builds list it. GBA DS drives the machine directly
# (emu/emu_armgba.c): no libretro layer, no Gambatte. ARMGBA is the submodule's path; ARMGBA_BACKEND picks the
# recompiler: a32 (the 3DS), x64 (the desktop test build).
ARMGBA_CORE := bios-image.c bus.c cheats.c gba.c memory.c state.c timing.c \
	audio/audio.c audio/audio-thread.c audio/psg.c cart/actionreplay.c cart/fcd.c cart/cart.c cart/ereader.c cart/gpio.c cart/overrides.c cart/savedata.c \
	cpu/arm.c cpu/decode.c cpu/step.c io/dma.c io/io.c io/rfu.c io/sio.c io/timer.c \
	video/render-thread.c video/render.c video/video.c cpu/jit.c cpu/$(ARMGBA_BACKEND)/backend.c
ifeq ($(ARMGBA_BACKEND),x64)
ARMGBA_CORE += video/render-x86.c
endif
ARMGBA_CORE_SRC := $(addprefix $(ARMGBA)/src/,$(ARMGBA_CORE))
# -fwrapv: armGBA's clocks are 32-bit counters compared by subtracting, which signed overflow must allow.
ARMGBA_CORE_FLAGS := -O3 -fwrapv -Wall -Wextra -Wno-unused-parameter -I$(ARMGBA)/src
