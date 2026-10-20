/* State Saver's emulator: GBA DS's own libmgba (same commit, same options)
 * compiled to WebAssembly, set up the way GBA DS's emu_open does it, so the
 * states it saves are byte for byte the ones GBA DS saves and loads
 * (mCoreSaveStateNamed with save data, RTC and metadata). */
#include <stdlib.h>
#include <string.h>

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/core/interface.h>
#include <mgba/gba/core.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#define EXPORT __attribute__((visibility("default")))
#define GBA_W 240
#define GBA_H 160

static struct mCore* core;
static uint32_t* fb;
static uint8_t* rom;
static struct VFile* saveVf;
static uint8_t* out;      /* the last state / save handed out */
static size_t outSize;
static char code[5], title[17];

static void quiet_log(struct mLogger* l, int cat, enum mLogLevel lvl, const char* fmt, va_list args) {
	(void) l, (void) cat, (void) lvl, (void) fmt, (void) args;
}
static struct mLogger quietLogger = { .log = quiet_log };

EXPORT void* ss_alloc(size_t n) {
	return malloc(n);
}

EXPORT void ss_free(void* p) {
	free(p);
}

static void close_core(void) {
	if (!core) return;
	mCoreConfigDeinit(&core->config);
	core->deinit(core); /* closes the ROM and save VFiles */
	core = NULL;
	saveVf = NULL;
	free(rom);
	rom = NULL;
}

/* The ROM (taken over: freed with the core) and, optionally, a battery save
 * to start from (copied). 1 on success. */
EXPORT int ss_load_rom(uint8_t* romData, size_t romLen, const uint8_t* sav, size_t savLen) {
	close_core();
	mLogSetDefaultLogger(&quietLogger);
	struct VFile* vf = VFileFromMemory(romData, romLen);
	if (!vf) return 0;
	core = mCoreFindVF(vf);
	if (!core || !core->init(core)) {
		vf->close(vf);
		core = NULL;
		return 0;
	}
	mCoreInitConfig(core, NULL);
	struct mCoreOptions opts = { .useBios = false, .volume = 0x100, .audioBuffers = 1024 };
	mCoreConfigLoadDefaults(&core->config, &opts);
	mCoreLoadConfig(core);
	if (!fb) fb = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	core->setVideoBuffer(core, (mColor*) fb, GBA_W);
	core->setAudioBufferSize(core, 2048);
	if (!core->loadROM(core, vf)) {
		close_core();
		return 0;
	}
	rom = romData;
	/* the battery save lives in memory; ss_save_export hands it out */
	saveVf = VFileMemChunk(sav, sav ? savLen : 0);
	core->loadSave(core, saveVf);
	core->reset(core);
	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	core->getGameInfo(core, &info);
	memcpy(code, info.code, 4);
	code[4] = 0;
	memcpy(title, info.title, 16);
	title[16] = 0;
	return 1;
}

EXPORT const char* ss_game_code(void) {
	return code;
}

EXPORT const char* ss_game_title(void) {
	return title;
}

EXPORT void ss_frame(uint32_t keys) {
	if (!core) return;
	core->setKeys(core, keys);
	core->runFrame(core);
}

EXPORT void ss_reset(void) {
	if (core) core->reset(core);
}

/* 240x160 pixels, 4 bytes each: R, G, B, (unused) */
EXPORT uint32_t* ss_video(void) {
	return fb;
}

EXPORT unsigned ss_audio_rate(void) {
	return core ? core->audioSampleRate(core) : 0;
}

/* Up to max stereo frames (int16 L, R) into buf; the number read. */
EXPORT int ss_audio(int16_t* buf, int max) {
	if (!core) return 0;
	struct mAudioBuffer* ab = core->getAudioBuffer(core);
	return ab ? (int) mAudioBufferRead(ab, buf, (size_t) max) : 0;
}

static size_t take(struct VFile* vf) {
	free(out);
	out = NULL;
	outSize = 0;
	ssize_t n = vf->size(vf);
	if (n > 0 && (out = malloc((size_t) n))) {
		vf->seek(vf, 0, SEEK_SET);
		outSize = (size_t) vf->read(vf, out, (size_t) n);
	}
	return outSize;
}

/* Saves a state the way GBA DS does (emu_state_save_file); its bytes are at
 * ss_out() afterwards. The size, 0 on failure. */
EXPORT size_t ss_state_save(void) {
	if (!core) return 0;
	struct VFile* vf = VFileMemChunk(NULL, 0);
	bool ok = mCoreSaveStateNamed(core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	size_t n = ok ? take(vf) : 0;
	vf->close(vf);
	return n;
}

/* Loads a state (GBA DS's .state / .ss files, or ss_state_save's): as
 * emu_state_load_file does. 1 on success. */
EXPORT int ss_state_load(const uint8_t* data, size_t len) {
	if (!core) return 0;
	struct VFile* vf = VFileFromConstMemory(data, len);
	bool ok = mCoreLoadStateNamed(core, vf, SAVESTATE_RTC);
	vf->close(vf);
	return ok;
}

/* The battery save as it is now; its bytes are at ss_out(). */
EXPORT size_t ss_save_export(void) {
	return saveVf ? take(saveVf) : 0;
}

EXPORT uint8_t* ss_out(void) {
	return out;
}

EXPORT uint8_t ss_read8(uint32_t addr) {
	return core ? (uint8_t) core->rawRead8(core, addr, -1) : 0;
}
