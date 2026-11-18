# GBA DS for the New Nintendo 3DS (see README.md). Nothing here needs devkitPro on this machine: the builds run in
# the devkitpro/devkitarm Docker image, the emulator tests in linuxserver/azahar.
#
#   make 3ds         ctr/gba-ds.3dsx
#   make 3ds LOG_URL=http://<this machine>:8377/log   a diagnosis build that sends its logs to tools/logserver.py
#   make cia         ctr/gba-ds.cia as well (makerom and bannertool in TOOLS, ~/3ds-tools/bin)
#   make sim         ../build/sim/harness: tools/harness.c on armGBA with its x86-64 recompiler (no console)
#   make 3ds-test    ctr/gba-ds-test.3dsx, with the capture test's profile
#   make azahar-test ROMS=<folder with Pokemon Emerald (USA, Europe)>   the test build in the Azahar emulator:
#                    screenshots in ../build/azahar (the game on the top screen, the hidden copy's menu on the bottom)
REPO := $(abspath ..)
DEVKITARM_IMAGE ?= devkitpro/devkitarm:latest
AZAHAR_IMAGE ?= lscr.io/linuxserver/azahar:latest
TOOLS ?= $(HOME)/3ds-tools/bin
VERSION := $(shell cat $(REPO)/3ds/VERSION)

.PHONY: 3ds 3ds-test cia sim azahar-test
# The repo is mounted whole: the build needs third_party/armgba and ../src. As the user who runs make, so the files
# it leaves are theirs.
CTR_RUN = docker run --rm -u $$(id -u):$$(id -g) -v "$(REPO):/repo" -v "$(TOOLS):/tools:ro" -w /repo/3ds/ctr
3ds:
	$(CTR_RUN) $(DEVKITARM_IMAGE) make -j4 $(if $(LOG_URL),LOG_URL=$(LOG_URL))
3ds-test:
	$(CTR_RUN) $(DEVKITARM_IMAGE) make -j4 TEST=1
cia: 3ds
	$(CTR_RUN) $(DEVKITARM_IMAGE) make cia-parts BANNERTOOL=/tools/bannertool
	$(CTR_RUN) ubuntu:24.04 sh cia.sh /tools/makerom $(VERSION)
sim:
	$(MAKE) -C sim

# A new folder for each run's screenshots and log (the container writes them as root).
OUT := $(REPO)/build/azahar/$(shell date +%Y%m%d-%H%M%S)
azahar-test: 3ds-test
	@test -n "$(ROMS)" || { echo "ROMS=<folder with the test's ROM>"; exit 1; }
	mkdir -p $(OUT)
	docker run --rm -e APP=gba-ds-test.3dsx -v "$(REPO):/repo" -v "$(abspath $(ROMS)):/roms:ro" -v "$(REPO)/3ds/test/sd:/sd:ro" -v "$(OUT):/out" \
		--entrypoint /bin/sh $(AZAHAR_IMAGE) /repo/3ds/test/azahar_run.sh \
		shot:01-list a wait:14 shot:02-game m wait:12 shot:03-capture 1 wait:2 shot:04-menu
	python3 test/xwd2png.py $(OUT)/*.xwd
	@echo "screenshots in $(OUT)"
