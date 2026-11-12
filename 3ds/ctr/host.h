/* What the 3DS itself gives GBA DS besides SDL's part (sdl_ctr.c): its log,
 * its model, its memory, its cores - and what armGBA asks of a 3DS host
 * (jitmem.c). */
#ifndef GBADS_CTR_HOST_H
#define GBADS_CTR_HOST_H

#include <stdbool.h>
#include <stddef.h>

/* armGBA's names for them (its 3DS host code calls these). */
void logLine(const char* format, ...) __attribute__((format(printf, 1, 2)));
bool isEmulator(void); /* Azahar and Citra */
bool isNew3ds(void);

size_t host_heap_free(void);
/* The core the app's side work runs on (the hidden copy of the game that
 * draws the second screen, a logo's extraction, files): the New 3DS's
 * fourth, else whichever the system gives. */
int host_side_core(void);
/* The next machine made is the game itself (the large share of the
 * recompiler's memory), or a copy. */
void host_log_checkpoint(void);
void host_ui_busy(void);
void host_fs_yield(void);
void host_mem_note(const char* what);
void host_jit_main(bool main, size_t romToLoad);
/* A folder on the SD card listed in one go: its entries (malloc'd, the
 * caller frees them), each with whether it's a folder and a file's size.
 * Returns how many, or -1 if the folder can't be read. */
typedef struct {
	char name[256];
	bool dir;
	long long size;
} HostEntry;
int host_read_dir(const char* path, HostEntry** out);
/* The first n bytes of a file on the SD card, with as few calls on it as
 * that takes. */
bool host_read_head(const char* path, void* out, size_t n);
/* Before the app exits. */
void jitHostRestoreMemory(void);

#endif
