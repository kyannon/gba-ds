/* Copyright (c) 2026 armGBA 3DS port contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// armGBA's platform/3ds/source/jitmem.c, changed for GBA DS: several machines at once (the game,
// the hidden copy that draws the second screen, a logo's), each with code memory of its own, where
// armGBA's frontend has one. What it does and why is as there: executable memory through
// svcControlProcessMemory (or Luma3DS's call, which is what Azahar answers), DaedalusX64-3DS's
// kernel routines for the caches, the render and sound threads on the New 3DS's other cores.

#include <3ds.h>

#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

Result svcLumaControlProcess(Handle process, u32 op, u32 arg2, u32 arg3);
s32 ctrEnableAllSvcsKernel(void);
void ctrFlushCachesKernel(void);
void ctrInvalidateICacheKernel(void);
void ctrFlushRangeKernel(void);
u32 jitFlushFrom, jitFlushTo;
Result svcLumaInvalidateInstructionCacheRange(u32 address, u32 size);

enum {
	PROCESSOP_SET_MMU_TO_RWX = 1,
	BUFFERS = 6,
};

// Each machine's code memory. Made executable with svcControlProcessMemory, so to be put back
// before the app exits: a process that ends with memory still read-write-execute hangs the system
// on "Closing software".
static struct {
	uint8_t* code;
	size_t size;
	bool protected;
} buffers[BUFFERS];
static LightLock lock;
static bool lockReady;
static Handle self;
// The next machine made is the game itself (host_jit_main): it gets the large share.
static bool wantMain;
int renderThreadCore = -1;
bool renderThreadSecond;

static void take(void) {
	if (!lockReady) {
		LightLock_Init(&lock);
		lockReady = true;
	}
	LightLock_Lock(&lock);
}

// romToLoad: what the game's ROM will take of the heap once this machine is made (0 if it is
// already in memory).
static size_t romPending;
void host_jit_main(bool main, size_t romToLoad) {
	wantMain = main;
	romPending = romToLoad;
}

static bool makeExecutable(uint8_t* code, size_t size, bool* protected) {
	if (!self && R_FAILED(svcDuplicateHandle(&self, CUR_PROCESS_HANDLE))) {
		self = 0;
	}
	Result rc = -1;
	if (!isEmulator()) {
		rc = svcControlProcessMemory(self, (u32) code, 0, size, MEMOP_PROT, MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE);
		if (R_FAILED(rc)) {
			logLine("jit: svcControlProcessMemory refused (%08lX): opening kernel calls", (unsigned long) rc);
			svcBackdoor(ctrEnableAllSvcsKernel);
			svcBackdoor(ctrEnableAllSvcsKernel);
			rc = svcControlProcessMemory(self, (u32) code, 0, size, MEMOP_PROT, MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE);
		}
	}
	Result rc2 = 0;
	if (R_FAILED(rc)) {
		rc2 = svcLumaControlProcess(self, PROCESSOP_SET_MMU_TO_RWX, 0, 0);
	}
	logLine("jit: svcControlProcessMemory %08lX%s, svcControlProcess(RWX) %08lX%s", (unsigned long) rc, isEmulator() ? " (skipped)" : "",
	        (unsigned long) rc2, R_FAILED(rc) ? "" : " (not needed)");
	if (R_FAILED(rc) && R_FAILED(rc2)) {
		return false;
	}
	*protected = R_SUCCEEDED(rc);
	return true;
}

// The game's code memory: the heap left after its ROM and HEAP_LATER, up to 16 MiB. The other machines run
// the same game for a few hundred frames at a time (a map screen, a title screen): up to 4 MiB
// each. What the recompiler keeps about each block is about 0.4 bytes for each byte of code.
enum {
	MAIN_MAX = 16 << 20,
	MAIN_MIN = 4 << 20,
	OTHER_MAX = 4 << 20,
	OTHER_MIN = 1 << 20,
	HEAP_LOW = 4 << 20, // (a state loaded takes two copies of the machine, 1.6 MiB, for a moment)
	// Kept for what is made after the game's code memory: the machines themselves and their render
	// and sound threads' copies, the map pictures, and
	// the hidden copy with its code.
	HEAP_LATER = 20 << 20,
};

bool jitHostMemoryLow(void) {
	// mallinfo walks the heap's free lists, so only every so often. A flush helps only if
	// compiling has taken memory since the last one.
	static int calls;
	static bool low;
	static size_t most;
	if (low || (calls++ & 15) == 0) {
		size_t const avail = host_heap_free();
		if (avail > most) {
			most = avail;
		}
		low = avail < HEAP_LOW && most > avail + (1 << 20);
		if (low) {
			most = 0;
		}
	}
	return low;
}

void* jitHostAllocCode(size_t* size) {
	take();
	bool const main = wantMain;
	wantMain = false;
	int slot = -1;
	for (int i = 0; i < BUFFERS; ++i) {
		if (!buffers[i].code) {
			slot = i;
			break;
		}
	}
	void* result = NULL;
	if (slot >= 0) {
		size_t const avail = host_heap_free();
		logLine("jit: %u KiB of the heap free, %u KiB wanted (%s)", (unsigned) (avail >> 10), (unsigned) (*size >> 10), main ? "the game" : "a copy");
		// The game: what is left once its ROM is loaded and the later machines have theirs. A copy:
		// half of what there is.
		size_t const reserve = main ? HEAP_LATER + romPending : HEAP_LOW;
		// (5/7: what the recompiler keeps about the code, 0.4 bytes a byte, comes out of the same heap
		// as the code fills)
		size_t spare = avail > reserve ? (main ? (avail - reserve) / 7 * 5 : (avail - reserve) / 2) : 0;
		romPending = 0;
		spare &= ~(size_t) 0xFFFFF;
		size_t const most = main ? MAIN_MAX : OTHER_MAX;
		size_t const least = main ? MAIN_MIN : OTHER_MIN;
		if (spare > *size) {
			spare = *size;
		}
		if (spare > most) {
			spare = most;
		}
		if (spare < least && avail > least + HEAP_LOW) {
			spare = least;
		}
		for (; spare >= least; spare -= 1 << 20) {
			uint8_t* code = memalign(0x1000, spare);
			if (!code) {
				continue;
			}
			bool protected = false;
			if (!makeExecutable(code, spare, &protected)) {
				free(code);
				break;
			}
			buffers[slot].code = code;
			buffers[slot].size = spare;
			buffers[slot].protected = protected;
			*size = spare;
			logLine("jit: %u KiB of code memory at %p (%s)", (unsigned) (spare >> 10), (void*) code, main ? "the game" : "a copy");
			result = code;
			break;
		}
	}
	if (!result) {
		logLine("jit: no executable memory: the recompiler can't run");
	}
	LightLock_Unlock(&lock);
	return result;
}

static void release(int i) {
	if (buffers[i].protected) {
		Result rc = svcControlProcessMemory(self, (u32) buffers[i].code, 0, buffers[i].size, MEMOP_PROT, MEMPERM_READ | MEMPERM_WRITE);
		logLine("jit: code memory back to read-write: %08lX", (unsigned long) rc);
	}
	free(buffers[i].code);
	memset(&buffers[i], 0, sizeof(buffers[i]));
}

// Before the app exits.
void jitHostRestoreMemory(void) {
	take();
	for (int i = 0; i < BUFFERS; ++i) {
		if (buffers[i].code && buffers[i].protected) {
			svcControlProcessMemory(self, (u32) buffers[i].code, 0, buffers[i].size, MEMOP_PROT, MEMPERM_READ | MEMPERM_WRITE);
			buffers[i].protected = false;
		}
	}
	LightLock_Unlock(&lock);
}

void jitHostFreeCode(void* code, size_t size) {
	(void) size;
	take();
	for (int i = 0; i < BUFFERS; ++i) {
		if (buffers[i].code == code) {
			release(i);
		}
	}
	LightLock_Unlock(&lock);
}

// The render thread on a core of its own: the New 3DS's third (core 2). At the game thread's
// priority. Not on an Old 3DS.
bool renderThreadHostCreate(pthread_t* thread, void* (*function)(void*), void* argument) {
	if (!isNew3ds()) {
		logLine("render thread: not on an Old 3DS");
		return false;
	}
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	Thread t = threadCreate((ThreadFunc) (void*) function, argument, 256 * 1024, priority, 2, false);
	renderThreadCore = 2;
	logLine("render thread on core %d: %s", renderThreadCore, t ? "started" : "couldn't start");
	if (!t) {
		renderThreadCore = -1;
		return false;
	}
	// libctru's pthread_t is its Thread, so pthread_join waits for this one.
	*thread = (pthread_t) t;
	return true;
}

// A second render worker (render_workers = 2 in gba-ds.cfg; armGBA asks every time): on the fourth
// core, at the game thread's priority, a step above the hidden copy's thread there. Each worker
// then draws every other line.
bool renderThreadHostCreateSecond(pthread_t* thread, void* (*function)(void*), void* argument) {
	renderThreadSecond = false;
	// (armGBA asks on the 3DS whatever it was started with: the setting is GBA DS's, emu.h)
	extern int emuRenderWorkers;
	if (renderThreadCore != 2 || emuRenderWorkers != 2) {
		return false;
	}
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	Thread t = threadCreate((ThreadFunc) (void*) function, argument, 256 * 1024, priority, 3, false);
	logLine("second render thread on core 3: %s", t ? "started" : "refused");
	if (!t) {
		return false;
	}
	renderThreadSecond = true;
	*thread = (pthread_t) t;
	return true;
}

// The sound thread beside the render worker, one step above it: it sleeps until it has a batch to
// mix, which takes it a fraction of a millisecond.
bool audioThreadHostCreate(pthread_t* thread, void* (*function)(void*), void* argument) {
	if (!isNew3ds()) {
		return false;
	}
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	Thread t = threadCreate((ThreadFunc) (void*) function, argument, 64 * 1024, priority - 1, 2, false);
	logLine("sound thread on core 2: %s", t ? "started" : "couldn't start");
	if (!t) {
		return false;
	}
	*thread = (pthread_t) t;
	return true;
}

void jitHostFlushCode(void* start, void* end) {
	u32 const from = (u32) start & ~31u;
	u32 const to = ((u32) end + 31) & ~31u;
	if (isEmulator()) {
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, from, to - from);
		svcLumaInvalidateInstructionCacheRange(from, to - from);
	} else if (to - from > 64 * 1024) {
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, from, to - from);
		ctrInvalidateICacheKernel();
	} else {
		// One range at a time: the kernel routine reads it from these two.
		take();
		jitFlushFrom = from;
		jitFlushTo = to;
		ctrFlushRangeKernel();
		LightLock_Unlock(&lock);
	}
}

uint64_t renderThreadHostTicks(void) {
	return svcGetSystemTick();
}
