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
bool android_data_record(char* out, size_t size) {
	const char* files = getenv("GBADS_APP_FILES");
	if (!files || !*files) return false;
	snprintf(out, size, "%s/data-folder.cfg", files);
	return true;
}
void android_data_setup(void) {}
