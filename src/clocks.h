/* The Performance setting (Linux): the CPU, memory and GPU clock governors
 * held at "performance" while GBA DS runs, or left to the system. */
#ifndef GBADS_CLOCKS_H
#define GBADS_CLOCKS_H
#include <stdbool.h>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
/* the setting means something here: a governor GBA DS can change (not on
 * BananaOS, whose banana-play.sh holds them at performance itself) */
bool clocks_available(void);
/* max: each governor at performance (what it was kept, also in a file
 * launch.sh puts back if GBA DS ends without doing so); else the ones
 * changed put back. Logged. */
void clocks_set(bool max);
#else
static inline bool clocks_available(void) { return false; }
static inline void clocks_set(bool max) { (void) max; }
#endif

#endif
