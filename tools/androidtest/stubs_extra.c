#include "android.h"
void android_second_set_interval(int i) { (void) i; }
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
void android_set_pinning(bool on) { (void) on; }
void android_pin_game_thread(void) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(1, &s); fprintf(stderr, "stub pin game: %d\n", sched_setaffinity(0, sizeof s, &s)); }
void android_pin_worker_thread(const char* w) { (void) w; cpu_set_t s; CPU_ZERO(&s); CPU_SET(0, &s); fprintf(stderr, "stub pin worker: %d\n", sched_setaffinity(0, sizeof s, &s)); }
int android_raise_thread(void) { return 0; }
/* GBA DS's folder (issue #34): data-folder.cfg in $GBADS_APP_FILES if set,
 * else none; the move itself isn't run here (android.c's android_data_setup) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool android_data_record(char* out, size_t size) {
	const char* files = getenv("GBADS_APP_FILES");
	if (!files || !*files) return false;
	snprintf(out, size, "%s/data-folder.cfg", files);
	return true;
}
void android_data_setup(void) {}
/* a hinge across one display (issue #48): STUB_HINGE=PIXELS when the
 * window's long side is STUB_HINGE_LONG (a Surface Duo spanned: 84, 2784) */
int android_hinge_gap(int w, int h) {
	const char* g = getenv("STUB_HINGE");
	const char* l = getenv("STUB_HINGE_LONG");
	int lw = w > h ? w : h;
	return g && (!l || atoi(l) == lw) ? atoi(g) : 0;
}

/* Settings > Rotation (issue #60): STUB_SECOND=none is one display; the mode is kept in memory */
static int stubRotation;
bool android_one_display(void) {
	const char* v = getenv("STUB_SECOND");
	return v && !strcmp(v, "none");
}
int android_rotation_get(void) { return stubRotation; }
void android_rotation_set(int mode) { stubRotation = mode; }
