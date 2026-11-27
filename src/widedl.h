/* GBA DS's widescreen patches, offered for download (docs/WIDESCREEN.md, WS-6).
 *
 * The patches live in the repository's widescreen/ folder, listed in
 * widescreen/index.txt; the app reads that list (fetched in the background
 * when a game's Patches menu opens, kept in <state_dir>/widescreen-index.txt
 * for the next time) and downloads a patch the player picks into the game's
 * patch folder, checked against the list's SHA-256. The web build has no
 * downloads: everything here is a no-op there. */
#ifndef GBADS_WIDEDL_H
#define GBADS_WIDEDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char code[5];
	int num, den;      /* the ratio */
	char path[300];    /* under widescreen/: CODE/NAME.ips */
	char file[256];    /* NAME.ips, as it's saved */
	char sha256[65];
	uint32_t crcs[8];  /* the ROM files it fits (none: any of the code's) */
	int nCrcs;
} WideEntry;

/* where the list and the patches come from: GBADS_WIDE_URL, else the
 * repository's widescreen/ folder on the branch the build was made from
 * (main for releases) */
const char* widedl_base(void);
/* start fetching the list, once per run (the cached one is read now) */
void widedl_refresh(const char* stateDir);
/* goes up each time a newer list has been read */
int widedl_generation(void);
/* the list's patches for this game: its code and its ROM file's CRC32 */
int widedl_for(const char* code, uint32_t fileCrc, WideEntry* out, int max);
/* the one nearest the screen's shape (w x h), within 2%; NULL if none */
const WideEntry* widedl_nearest(const WideEntry* e, int n, int w, int h);
/* download e into dir (blocking; a few KB), checked; false and err if not */
bool widedl_fetch(const WideEntry* e, const char* dir, char* err, size_t errSize);

#endif
