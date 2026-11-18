/* What the 3DS build leaves out of GBA DS: EmulationStation and BananaOS
 * (other systems' menus). */
#include "esinteg.h"

#include <stdio.h>

bool es_present(const Config* cfg) {
	(void) cfg;
	return false;
}
bool es_integration_on(const Config* cfg) {
	(void) cfg;
	return false;
}
bool es_integration_set(const Config* cfg, bool on, char* msg, size_t msgSize) {
	(void) cfg;
	(void) on;
	if (msgSize) snprintf(msg, msgSize, "not on a 3DS");
	return false;
}
void es_integration_refresh(const Config* cfg) {
	(void) cfg;
}
bool banana_present(void) {
	return false;
}
bool banana_integration_on(void) {
	return false;
}
bool banana_integration_set(bool on, char* msg, size_t msgSize) {
	(void) on;
	if (msgSize) snprintf(msg, msgSize, "not on a 3DS");
	return false;
}
void banana_integration_refresh(void) {
}

/* The C library here is without these. */
#include <libgen.h>
#include <string.h>

char* dirname(char* path) {
	static char dot[] = ".";
	if (!path || !*path) return dot;
	size_t n = strlen(path);
	while (n > 1 && path[n - 1] == '/') path[--n] = 0;
	char* slash = strrchr(path, '/');
	if (!slash) return dot;
	if (slash == path) {
		path[1] = 0;
		return path;
	}
	*slash = 0;
	return path;
}

char* basename(char* path) {
	static char dot[] = ".";
	if (!path || !*path) return dot;
	size_t n = strlen(path);
	while (n > 1 && path[n - 1] == '/') path[--n] = 0;
	char* slash = strrchr(path, '/');
	return slash && slash[1] ? slash + 1 : path;
}

FILE* popen(const char* command, const char* mode) {
	(void) command;
	(void) mode;
	return NULL;
}

int pclose(FILE* f) {
	(void) f;
	return -1;
}
