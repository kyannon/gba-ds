/* A folder picker (Linux, issue #39; Android too, with All files access, issue #111): a screen of its own listing the
 * folders in one, until one is chosen or it's left. The D-pad (or a tap)
 * picks a folder and A opens it, B goes up a folder, "Use This Folder"
 * chooses the one shown. Hidden folders aren't listed. */
#ifndef GBADS_FOLDERPICK_H
#define GBADS_FOLDERPICK_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	FOLDER_CANCEL, /* left as it was */
	FOLDER_CHOSEN, /* out: the folder */
	FOLDER_AUTO,   /* "Automatic" (only offered with autoLabel) */
} FolderPick;

/* title: the screen's heading; start: where it opens (the nearest folder
 * of it that exists, else home); count: a file name ending counted in the
 * folder shown, to say how many there are ("<n> RetroArch states here"; the
 * game list's one use), or none; autoLabel: a third button's text (the setting's automatic value),
 * or NULL for none. */
FolderPick folder_pick(const char* title, const char* start, const char* count, bool showCount, const char* autoLabel,
                       char* out, size_t n);

#endif
