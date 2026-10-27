/* Pokemon Pinball cost of more cores (docs/PINBALL.md):
 *   bench ROM STATE FRAMES
 * Times emu_frame for one core, three cores in a row, and the game then two
 * copies on two threads at once; and a state save + load. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "emu.h"

static double now(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void* run1(void* p) { emu_frame((Emu*) p, 0); return NULL; }

int main(int argc, char** argv) {
	Emu c[3] = {{0}};
	for (int i = 0; i < 3; ++i) {
		if (!emu_open(&c[i], argv[1], NULL, false) || !emu_state_load_file(&c[i], argv[2])) return 1;
	}
	int n = atoi(argv[3]);
	double t0 = now();
	for (int f = 0; f < n; ++f) emu_frame(&c[0], 0);
	double one = (now() - t0) / n;
	t0 = now();
	for (int f = 0; f < n; ++f) for (int i = 0; i < 3; ++i) emu_frame(&c[i], 0);
	double three = (now() - t0) / n;
	t0 = now();
	for (int f = 0; f < n; ++f) {
		emu_frame(&c[0], 0);
		pthread_t t;
		pthread_create(&t, NULL, run1, &c[1]);
		emu_frame(&c[2], 0);
		pthread_join(t, NULL);
	}
	double par = (now() - t0) / n;
	static uint8_t buf[1 << 20];
	t0 = now();
	for (int f = 0; f < n; ++f) { emu_state_save(&c[0], buf); emu_state_load(&c[1], buf); }
	double st = (now() - t0) / n;
	printf("ms a frame: one core %.2f, three in a row %.2f, game then two copies on two threads %.2f; state save+load %.3f\n", one, three, par, st);
	return 0;
}
