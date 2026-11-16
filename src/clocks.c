/* The Performance setting (issue #50): "Set to max" holds the CPU, memory
 * controller and GPU at their fastest while GBA DS runs; "Let the system
 * decide" leaves the governors as they are (EmulationStation's choice on
 * ROCKNIX). Under a governor that lets the clock drop between frames, a
 * busy frame can come too late on some devices: stutters and sound gaps.
 * Held at max, the battery runs down faster and the device gets warmer.
 *
 * What each governor was is kept in memory and in GOV_STATE, which
 * launch.sh reads and puts back if GBA DS ends without doing so (a crash,
 * being killed). Only the governors GBA DS changed are ever put back. */
#include "clocks.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GOV_STATE "/tmp/gba-ds-governors.state"
#define GOV_MAX 16

static struct {
	char path[160];
	char was[40];
} changed[GOV_MAX];
static int nChanged;

/* (testing: GBADS_SYSFS, a folder standing in for /sys) */
static const char* sysfs(void) {
	const char* r = getenv("GBADS_SYSFS");
	return r && r[0] ? r : "/sys";
}
static const char* short_name(const char* path) {
	const char* p = strstr(path, "cpufreq/");
	if (p) return p + 8;
	p = strstr(path, "devfreq/");
	return p ? p + 8 : path;
}

/* the governor files: every CPU policy, and the memory controller's and the
 * GPU's devfreq (not the NPU's or others': nothing GBA DS uses) */
static int governors(char paths[][160], int max) {
	int n = 0;
	char dir[200];
	snprintf(dir, sizeof(dir), "%s/devices/system/cpu/cpufreq", sysfs());
	DIR* d = opendir(dir);
	struct dirent* e;
	while (d && (e = readdir(d)) && n < max) {
		if (strncmp(e->d_name, "policy", 6)) continue;
		snprintf(paths[n++], 160, "%s/%s/scaling_governor", dir, e->d_name);
	}
	if (d) closedir(d);
	snprintf(dir, sizeof(dir), "%s/class/devfreq", sysfs());
	d = opendir(dir);
	while (d && (e = readdir(d)) && n < max) {
		if (e->d_name[0] == '.' || (!strstr(e->d_name, "dmc") && !strstr(e->d_name, "gpu"))) continue;
		snprintf(paths[n++], 160, "%s/%s/governor", dir, e->d_name);
	}
	if (d) closedir(d);
	return n;
}

static bool read_line(const char* path, char* out, size_t n) {
	FILE* f = fopen(path, "r");
	if (!f) return false;
	bool ok = fgets(out, (int) n, f) != NULL;
	fclose(f);
	if (ok) out[strcspn(out, "\n")] = 0;
	return ok && out[0];
}

static bool write_line(const char* path, const char* val) {
	FILE* f = fopen(path, "w");
	if (!f) return false;
	bool ok = fprintf(f, "%s\n", val) > 0;
	return fclose(f) == 0 && ok;
}

/* the governor that holds this one at its fastest: "performance", or one
 * named like it (some memory controllers': dmc_performance) */
static bool performance_name(const char* path, char* out, size_t n) {
	char avail[400], dir[160];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	if (!slash) return false;
	*slash = 0;
	char file[200];
	snprintf(file, sizeof(file), "%s/%s", dir, strstr(path, "scaling_governor") ? "scaling_available_governors" : "available_governors");
	if (!read_line(file, avail, sizeof(avail))) {
		snprintf(out, n, "performance"); /* (not listed: try it) */
		return true;
	}
	char* save = NULL;
	char* fallback = NULL;
	for (char* t = strtok_r(avail, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
		if (!strcmp(t, "performance")) {
			snprintf(out, n, "%s", t);
			return true;
		}
		if (!fallback && strstr(t, "performance")) fallback = t;
	}
	if (fallback) snprintf(out, n, "%s", fallback);
	return fallback != NULL;
}

static bool stateMine; /* GOV_STATE is ours (v1.5.3's launch.sh wrote its own: left alone) */

static void save_state(void) {
	if (!nChanged) {
		if (stateMine) unlink(GOV_STATE);
		stateMine = false;
		return;
	}
	FILE* f = fopen(GOV_STATE, "w");
	if (!f) return;
	stateMine = true;
	for (int i = 0; i < nChanged; ++i) fprintf(f, "%s\t%s\n", changed[i].path, changed[i].was);
	fclose(f);
}

bool clocks_available(void) {
	if (access("/etc/banana", F_OK) == 0) return false;
	char paths[GOV_MAX][160];
	int n = governors(paths, GOV_MAX);
	for (int i = 0; i < n; ++i) {
		if (access(paths[i], W_OK) == 0) return true;
	}
	return false;
}

void clocks_set(bool max) {
	if (access("/etc/banana", F_OK) == 0) return;
	char paths[GOV_MAX][160];
	int n = governors(paths, GOV_MAX);
	char log[1200] = "";
	size_t len = 0;
	if (max) {
		for (int i = 0; i < n && nChanged < GOV_MAX; ++i) {
			char was[40], perf[40];
			if (!read_line(paths[i], was, sizeof(was)) || !performance_name(paths[i], perf, sizeof(perf))) continue;
			bool mine = false;
			for (int k = 0; k < nChanged; ++k) mine |= !strcmp(changed[k].path, paths[i]);
			if (mine || !strcmp(was, perf)) continue;
			if (!write_line(paths[i], perf)) continue;
			snprintf(changed[nChanged].path, sizeof(changed[nChanged].path), "%s", paths[i]);
			snprintf(changed[nChanged].was, sizeof(changed[nChanged].was), "%s", was);
			++nChanged;
			len += (size_t) snprintf(log + len, len < sizeof(log) ? sizeof(log) - len : 0, " %s %s->%s",
			                         short_name(paths[i]), was, perf);
			if (len >= sizeof(log)) len = sizeof(log) - 1;
		}
		save_state();
		fprintf(stderr, "performance: set to max%s%s\n", nChanged ? ":" : " (the governors were already at performance)", log);
	} else {
		for (int i = 0; i < nChanged; ++i) {
			write_line(changed[i].path, changed[i].was);
			len += (size_t) snprintf(log + len, len < sizeof(log) ? sizeof(log) - len : 0, " %s",
			                         changed[i].was);
			if (len >= sizeof(log)) len = sizeof(log) - 1;
		}
		bool had = nChanged > 0;
		nChanged = 0;
		save_state();
		/* what the system has chosen, for reports */
		char now[600] = "";
		size_t nl = 0;
		for (int i = 0; i < n; ++i) {
			char g[40];
			if (!read_line(paths[i], g, sizeof(g))) continue;
			nl += (size_t) snprintf(now + nl, nl < sizeof(now) ? sizeof(now) - nl : 0, " %s=%s",
			                        short_name(paths[i]), g);
			if (nl >= sizeof(now)) nl = sizeof(now) - 1;
		}
		fprintf(stderr, "performance: the system decides%s;%s\n", had ? " (governors put back)" : "", now[0] ? now : " no governors found");
	}
}
#endif
