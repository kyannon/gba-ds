#include "esinteg.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CFG_NAME "es_systems_gbads.cfg"
#define SCRIPT_NAME "gbads-launch.sh"
#define EMU_NAME "gbads"
#define RUNEMU "/usr/bin/runemu.sh"

static void join(char* out, size_t n, const char* dir, const char* name) {
	snprintf(out, n, "%s/%s", dir, name);
}

bool es_integration_on(const Config* cfg) {
	char p[700];
	join(p, sizeof(p), cfg->esConfigDir, CFG_NAME);
	struct stat st;
	return stat(p, &st) == 0;
}

static char* read_file(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = n > 0 && n < (32 << 20) ? malloc(n + 1) : NULL;
	if (buf && fread(buf, 1, n, f) == (size_t) n) {
		buf[n] = 0;
	} else {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

/* Writes via a temporary file, so a reader never sees half a file. */
static bool write_file(const char* path, const char* text, mode_t mode) {
	char tmp[800];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f) return false;
	bool ok = fputs(text, f) >= 0;
	ok = fclose(f) == 0 && ok;
	if (ok) chmod(tmp, mode);
	if (ok && rename(tmp, path) != 0) ok = false;
	if (!ok) unlink(tmp);
	return ok;
}

/* The text between the first open and the following close inside
 * [from, to), or NULL. */
static const char* between(const char* from, const char* to, const char* open, const char* close, size_t* len) {
	const char* p = strstr(from, open);
	if (!p || p >= to) return NULL;
	p += strlen(open);
	const char* e = strstr(p, close);
	if (!e || e > to) return NULL;
	*len = (size_t) (e - p);
	return p;
}

/* The <system> element whose <name> is gba, as [start, end). */
static bool find_gba_system(const char* xml, const char** start, const char** end) {
	for (const char* p = strstr(xml, "<system>"); p; p = strstr(p + 1, "<system>")) {
		const char* e = strstr(p, "</system>");
		if (!e) return false;
		size_t n;
		const char* name = between(p, e, "<name>", "</name>", &n);
		while (name && n && (*name == ' ' || *name == '\t' || *name == '\n' || *name == '\r')) ++name, --n;
		while (name && n && (name[n - 1] == ' ' || name[n - 1] == '\t' || name[n - 1] == '\n' || name[n - 1] == '\r')) --n;
		if (name && n == 3 && !strncmp(name, "gba", 3)) {
			*start = p;
			*end = e;
			return true;
		}
		p = e;
	}
	return false;
}

/* This program's own folder (where launch.sh and es-play.sh are). */
static bool app_dir(char* out, size_t n) {
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0) return false;
	exe[len] = 0;
	char* slash = strrchr(exe, '/');
	if (!slash) return false;
	*slash = 0;
	snprintf(out, n, "%s", exe);
	return true;
}

static const char* const SCRIPT_TEXT =
	"#!/bin/bash\n"
	"# Written by GBA DS (game list > Settings > GBA DS in EmulationStation) and\n"
	"# removed when that's switched off. EmulationStation runs this for GBA games\n"
	"# instead of " RUNEMU " (see " CFG_NAME " next to it). With GBA DS picked\n"
	"# as the emulator, the game starts in GBA DS, which runemu.sh runs as a Port\n"
	"# (so ROCKNIX's settings for Ports apply); anything else goes to runemu.sh\n"
	"# unchanged.\n"
	"GBADS_DIR='%s'\n"
	"case \" $* \" in\n"
	"*\" --emulator=" EMU_NAME " \"*)\n"
	"\tROM=\"$1\"\n"
	"\tshift\n"
	"\targs=()\n"
	"\tif [ -f \"$GBADS_DIR/es-play.sh\" ]; then\n"
	"\t\texport GBADS_ROM=\"$ROM\"\n"
	"\t\tfor a in \"$@\"; do\n"
	"\t\t\tcase \"$a\" in\n"
	"\t\t\t-P*) args+=(-Pports) ;;\n"
	"\t\t\t*) args+=(\"$a\") ;;\n"
	"\t\t\tesac\n"
	"\t\tdone\n"
	"\t\texec " RUNEMU " \"$GBADS_DIR/es-play.sh\" \"${args[@]}\"\n"
	"\tfi\n"
	"\t# GBA DS isn't there any more: play the game in mGBA instead\n"
	"\tfor a in \"$@\"; do\n"
	"\t\tcase \"$a\" in\n"
	"\t\t--emulator=*) args+=(--emulator=retroarch) ;;\n"
	"\t\t--core=*) args+=(--core=mgba) ;;\n"
	"\t\t*) args+=(\"$a\") ;;\n"
	"\t\tesac\n"
	"\tdone\n"
	"\texec " RUNEMU " \"$ROM\" \"${args[@]}\"\n"
	"\t;;\n"
	"esac\n"
	"exec " RUNEMU " \"$@\"\n";

/* es_systems.cfg as EmulationStation reads it: the user's copy first. */
static char* read_es_systems(const Config* cfg, char* used, size_t usedSize) {
	char first[700];
	join(first, sizeof(first), cfg->esConfigDir, "es_systems.cfg");
	const char* candidates[] = { first, "/storage/.emulationstation/es_systems.cfg", "/etc/emulationstation/es_systems.cfg",
		                         "/usr/config/emulationstation/es_systems.cfg" };
	for (size_t i = 0; i < sizeof(candidates) / sizeof(*candidates); ++i) {
		char* xml = read_file(candidates[i]);
		if (xml) {
			snprintf(used, usedSize, "%s", candidates[i]);
			return xml;
		}
	}
	return NULL;
}

bool es_present(const Config* cfg) {
	if (es_integration_on(cfg)) return true;
	char used[700];
	char* xml = read_es_systems(cfg, used, sizeof(used));
	free(xml);
	return xml != NULL;
}

static bool install(const Config* cfg, char* msg, size_t msgSize) {
	char dir[600];
	if (!app_dir(dir, sizeof(dir))) {
		snprintf(msg, msgSize, "Can't find the GBA DS folder.");
		return false;
	}
	char src[700];
	char* xml = read_es_systems(cfg, src, sizeof(src));
	if (!xml) {
		snprintf(msg, msgSize, "No es_systems.cfg found.");
		return false;
	}
	const char *sys, *sysEnd;
	size_t cmdLen = 0, emuLen = 0;
	const char* cmd = NULL;
	const char* emus = NULL;
	if (find_gba_system(xml, &sys, &sysEnd)) {
		cmd = between(sys, sysEnd, "<command>", "</command>", &cmdLen);
		emus = between(sys, sysEnd, "<emulators>", "</emulators>", &emuLen);
	}
	const char* run = cmd ? strstr(cmd, RUNEMU) : NULL;
	if (!cmd || !emus || !run || run >= cmd + cmdLen) {
		fprintf(stderr, "es: no usable gba system in %s\n", src);
		snprintf(msg, msgSize, "Unexpected es_systems.cfg (see the log).");
		free(xml);
		return false;
	}

	char script[700], cfgPath[700];
	join(script, sizeof(script), cfg->esConfigDir, SCRIPT_NAME);
	join(cfgPath, sizeof(cfgPath), cfg->esConfigDir, CFG_NAME);

	/* the launch command with runemu.sh swapped for our script */
	size_t pre = (size_t) (run - cmd), post = cmdLen - pre - strlen(RUNEMU);
	/* the emulator list without any old gbads entry, plus ours */
	size_t cap = cmdLen + emuLen + strlen(script) + 4096;
	char* out = malloc(cap);
	if (!out) {
		free(xml);
		return false;
	}
	int n = snprintf(out, cap,
	                 "<?xml version=\"1.0\"?>\n"
	                 "<!-- Written by GBA DS (game list > Settings > GBA DS in EmulationStation), from\n"
	                 "     %s. EmulationStation merges it into its GBA system: the emulator list\n"
	                 "     gains \"" EMU_NAME "\" and games start through " SCRIPT_NAME ". Delete this file\n"
	                 "     (or switch the setting off) to undo. -->\n"
	                 "<systemList>\n"
	                 "\t<system>\n"
	                 "\t\t<name>gba</name>\n"
	                 "\t\t<command>%.*s%s%.*s</command>\n"
	                 "\t\t<emulators>",
	                 src, (int) pre, cmd, script, (int) post, run + strlen(RUNEMU));
	const char* e = emus;
	const char* eEnd = emus + emuLen;
	while (e < eEnd) {
		const char* tag = strstr(e, "<emulator");
		if (!tag || tag >= eEnd) break;
		const char* gt = strchr(tag, '>');
		if (!gt || gt >= eEnd) break;
		const char* stop;
		if (gt[-1] == '/') {
			stop = gt + 1; /* <emulator ... /> */
		} else {
			const char* close = strstr(gt, "</emulator>");
			if (!close || close >= eEnd) break;
			stop = close + strlen("</emulator>");
		}
		const char* nm = strstr(tag, "name=\"" EMU_NAME "\"");
		bool ours = nm && nm < gt;
		n += snprintf(out + n, cap - n, "%.*s", (int) (tag - e), e); /* the whitespace before it */
		if (!ours) n += snprintf(out + n, cap - n, "%.*s", (int) (stop - tag), tag);
		e = stop;
	}
	n += snprintf(out + n, cap - n,
	              "\n\t\t\t<emulator name=\"" EMU_NAME "\">\n"
	              "\t\t\t\t<cores>\n"
	              "\t\t\t\t\t<core default=\"false\">" EMU_NAME "</core>\n"
	              "\t\t\t\t</cores>\n"
	              "\t\t\t</emulator>\n"
	              "\t\t</emulators>\n"
	              "\t</system>\n"
	              "</systemList>\n");
	free(xml);

	char scriptText[4096];
	snprintf(scriptText, sizeof(scriptText), SCRIPT_TEXT, dir);
	bool ok = write_file(script, scriptText, 0755) && write_file(cfgPath, out, 0644);
	free(out);
	if (!ok) {
		fprintf(stderr, "es: writing %s: %s\n", cfgPath, strerror(errno));
		snprintf(msg, msgSize, "Couldn't write to %s.", cfg->esConfigDir);
		unlink(cfgPath);
		return false;
	}
	fprintf(stderr, "es: wrote %s and %s (from %s)\n", cfgPath, script, src);
	snprintf(msg, msgSize, "On. Restart EmulationStation to see it.");
	return true;
}

bool es_integration_set(const Config* cfg, bool on, char* msg, size_t msgSize) {
	if (on) return install(cfg, msg, msgSize);
	char p[700];
	join(p, sizeof(p), cfg->esConfigDir, CFG_NAME);
	bool ok = unlink(p) == 0 || errno == ENOENT;
	join(p, sizeof(p), cfg->esConfigDir, SCRIPT_NAME);
	unlink(p);
	snprintf(msg, msgSize, ok ? "Off. Restart EmulationStation to apply." : "Couldn't remove the setting file.");
	return ok;
}

void es_integration_refresh(const Config* cfg) {
	if (!es_integration_on(cfg)) return;
	char msg[128];
	if (!install(cfg, msg, sizeof(msg))) fprintf(stderr, "es: refresh failed: %s\n", msg);
}

/* ---------------------------------------------------------------- BananaOS */

#ifndef BANANA_DEFAULT /* (overridden by tests) */
#define BANANA_DEFAULT "/etc/banana/systems.conf"
#define BANANA_USER "/data/banana/systems.conf"
#endif
#define BANANA_HEADER "# gba-ds: copied from " BANANA_DEFAULT " by GBA DS"
#define BANANA_MARK "# gba-ds: run = "

bool banana_present(void) {
	struct stat st;
	return stat("/etc/banana", &st) == 0;
}

bool banana_integration_on(void) {
	char* t = read_file(BANANA_USER);
	bool on = t && strstr(t, BANANA_MARK) != NULL;
	free(t);
	return on;
}

/* The [gba] section's "run = ..." line: start and end (after its newline). */
static bool gba_run_line(const char* t, const char** start, const char** end) {
	const char* sec = strstr(t, "[gba]");
	while (sec && sec != t && sec[-1] != '\n') sec = strstr(sec + 1, "[gba]");
	if (!sec) return false;
	const char* p = strchr(sec, '\n');
	while (p && *++p && *p != '[') {
		const char* nl = strchr(p, '\n');
		if (!nl) nl = p + strlen(p);
		const char* q = p;
		while (*q == ' ' || *q == '\t') ++q;
		if (!strncmp(q, "run", 3) && (q[3] == ' ' || q[3] == '=' || q[3] == '\t')) {
			*start = p;
			*end = *nl ? nl + 1 : nl;
			return true;
		}
		p = *nl ? nl : NULL;
		if (!p) break;
	}
	return false;
}

static bool banana_on(char* msg, size_t msgSize) {
	char dir[600];
	if (!app_dir(dir, sizeof(dir))) {
		snprintf(msg, msgSize, "Couldn't find GBA DS's folder.");
		return false;
	}
	bool copy = access(BANANA_USER, F_OK) != 0;
	char* t = read_file(copy ? BANANA_DEFAULT : BANANA_USER);
	const char *a, *b;
	if (!t || !gba_run_line(t, &a, &b)) {
		free(t);
		snprintf(msg, msgSize, "BananaOS's systems list has no Game Boy Advance entry.");
		return false;
	}
	/* the original run line, kept as a comment, then GBA DS's */
	const char* v = strchr(a, '=');
	v = v && v < b ? v + 1 : b;
	while (*v == ' ' || *v == '\t') ++v;
	size_t vlen = (size_t) (b - v);
	while (vlen && (v[vlen - 1] == '\n' || v[vlen - 1] == '\r' || v[vlen - 1] == ' ')) --vlen;
	size_t n = strlen(t) + vlen + strlen(dir) + 512;
	char* out = malloc(n);
	snprintf(out, n, "%s%.*s" BANANA_MARK "%.*s\nrun = bash '%s/banana-play.sh' %%ROM%%\n%s",
	         copy ? BANANA_HEADER " (game list > Settings: GBA DS in BananaOS's GBA list).\n"
	                "# Switching that off puts the Game Boy Advance line back, and deletes this copy\n"
	                "# if nothing else in it was changed.\n" : "",
	         (int) (a - t), t, (int) vlen, v, dir, b);
	bool ok = write_file(BANANA_USER, out, 0644);
	free(out);
	free(t);
	if (!ok) {
		snprintf(msg, msgSize, "Couldn't write %s.", BANANA_USER);
		return false;
	}
	fprintf(stderr, "banana: GBA DS set for the Game Boy Advance list in %s\n", BANANA_USER);
	snprintf(msg, msgSize, "On. Restart BananaOS (Settings > System) for its menu to use it.");
	return true;
}

static bool banana_off(char* msg, size_t msgSize) {
	char* t = read_file(BANANA_USER);
	if (!t || !strstr(t, BANANA_MARK)) {
		free(t);
		snprintf(msg, msgSize, "Off.");
		return true;
	}
	/* "# gba-ds: run = ORIG" + the "run = ..." after it -> "run = ORIG" */
	char* m = strstr(t, BANANA_MARK);
	char* mEnd = strchr(m, '\n');
	char* next = mEnd ? strchr(mEnd + 1, '\n') : NULL;
	size_t n = strlen(t) + 16;
	char* out = malloc(n);
	snprintf(out, n, "%.*srun = %.*s\n%s", (int) (m - t), t, (int) ((mEnd ? mEnd : m + strlen(m)) - (m + strlen(BANANA_MARK))),
	         m + strlen(BANANA_MARK), next ? next + 1 : "");
	/* the copy GBA DS made: its header out, and gone if it's the default again */
	bool ours = strncmp(out, BANANA_HEADER, strlen(BANANA_HEADER)) == 0;
	char* body = out;
	if (ours) {
		for (int k = 0; k < 3 && body; ++k) {
			char* nl = strchr(body, '\n');
			body = nl ? nl + 1 : NULL;
		}
		if (!body) body = out + strlen(out);
	}
	char* def = ours ? read_file(BANANA_DEFAULT) : NULL;
	bool ok;
	if (def && !strcmp(def, body)) {
		ok = unlink(BANANA_USER) == 0;
		fprintf(stderr, "banana: removed %s (back to BananaOS's own list)\n", BANANA_USER);
	} else {
		ok = write_file(BANANA_USER, body, 0644);
		fprintf(stderr, "banana: Game Boy Advance list back to its emulator in %s\n", BANANA_USER);
	}
	free(def);
	free(out);
	free(t);
	snprintf(msg, msgSize, ok ? "Off. Restart BananaOS (Settings > System) for its menu to use it." : "Couldn't change %s.",
	         BANANA_USER);
	return ok;
}

bool banana_integration_set(bool on, char* msg, size_t msgSize) {
	if (on && banana_integration_on()) {
		snprintf(msg, msgSize, "On.");
		return true;
	}
	return on ? banana_on(msg, msgSize) : banana_off(msg, msgSize);
}

void banana_integration_refresh(void) {
	/* GBA DS moved since it was switched on: point the line at its new folder */
	if (!banana_present() || !banana_integration_on()) return;
	char dir[600], want[700];
	if (!app_dir(dir, sizeof(dir))) return;
	snprintf(want, sizeof(want), "'%s/banana-play.sh'", dir);
	char* t = read_file(BANANA_USER);
	bool current = t && strstr(t, want);
	free(t);
	if (current) return;
	char msg[160];
	if (!banana_off(msg, sizeof(msg)) || !banana_on(msg, sizeof(msg))) fprintf(stderr, "banana: refresh failed: %s\n", msg);
}
