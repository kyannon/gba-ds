/* desktop stand-ins for android.c's update parts: the "installer" copies the
 * APK to $STUB_INSTALLED and answers with $STUB_INSTALL (1 confirm shown,
 * -2 cancelled, -3 another key, -1 failure) after a moment */
#include "android.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void android_http_cancel(bool on) { if (on) fprintf(stderr, "stub: http cancel\n"); }
static Uint32 handedAt;
bool android_install_apk(const char* path, char* err, size_t n) {
	char cmd[2400];
	snprintf(cmd, sizeof cmd, "cp '%s' '%s'", path, getenv("STUB_INSTALLED") ? getenv("STUB_INSTALLED") : "/dev/null");
	if (system(cmd) != 0) { snprintf(err, n, "stub copy failed"); return false; }
	fprintf(stderr, "stub: installer got %s\n", path);
	handedAt = SDL_GetTicks();
	return true;
}
int android_take_install_result(char* msg, size_t n) {
	if (!handedAt || SDL_GetTicks() - handedAt < 1500) return 0;
	handedAt = 0;
	snprintf(msg, n, "stub message");
	return getenv("STUB_INSTALL") ? atoi(getenv("STUB_INSTALL")) : 1;
}
float android_display_ppi(int view) { (void) view; return 0; }
