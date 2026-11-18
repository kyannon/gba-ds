#include "esde.h"
#include "i18n.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------ what GBA DS puts in */

#define CMD_OPEN "<command label=\"GBA DS (Standalone)\">"
/* LauncherActivity reads the extra "rom" as a file path: patches beside the
 * game then work (they don't with a content link, %ROMPROVIDER%) */
#define CMD_LINE "        " CMD_OPEN "%EMULATOR_GBA-DS% %ACTIVITY_CLEAR_TASK% %ACTIVITY_CLEAR_TOP% %EXTRA_rom%=%ROM%</command>\n"

#define COPY_MARK "gba-ds: ES-DE's own gba entry"
#define COPY_NOTE                                                                                              \
	"        <!-- " COPY_MARK ", copied by GBA DS (Settings > GBA DS in ES-DE) with GBA DS put first.\n" \
	"             GBA DS brings it up to date when it starts, and takes it out when that's switched off. -->\n"

/* ES-DE's own gba system for Android (resources/systems/android/es_systems.xml,
 * ES-DE 3.4, Sep 2026). With RetroArch in SAF mode its RetroArch lines end
 * in %ROMSAF% instead (es_systems_saf.xml: the only difference). */
static const char* const GBA_BODY =
	"        <fullname>Nintendo Game Boy Advance</fullname>\n"
	"        <path>%ROMPATH%/gba</path>\n"
	"        <extension>.agb .AGB .bin .BIN .cgb .CGB .dmg .DMG .gb .GB .gba .GBA .gbc .GBC .gbx .GBX .sgb .SGB .7z .7Z .zip .ZIP</extension>\n"
	CMD_LINE
#define RA(label, core) \
	"        <command label=\"" label "\">%EMULATOR_RETROARCH% %EXTRA_CONFIGFILE%=%EXTERNALDATA%/Android/data/%ANDROIDPACKAGE%/files/retroarch.cfg " \
	"%EXTRA_LIBRETRO%=%INTERNALDATA%/%ANDROIDPACKAGE%/cores/" core "_libretro_android.so %EXTRA_ROM%=%ROM%</command>\n"
	RA("mGBA", "mgba")
	RA("VBA-M", "vbam")
	RA("VBA Next", "vba_next")
	RA("gpSP", "gpsp")
	RA("NooDS", "noods")
	"        <command label=\"NooDS (Standalone)\">%EMULATOR_NOODS% %ACTIVITY_CLEAR_TASK% %ACTIVITY_CLEAR_TOP% %EXTRA_LaunchPath%=%ROM%</command>\n"
	RA("SkyEmu", "skyemu")
	"        <command label=\"SkyEmu (Standalone)\">%EMULATOR_SKYEMU% %ACTIVITY_CLEAR_TASK% %ACTIVITY_CLEAR_TOP% %ACTION%=android.intent.action.VIEW %DATA%=%ROMPROVIDER%</command>\n"
	RA("MesenCE", "mesen2")
	"        <command label=\"GBA.emu (Standalone)\">%EMULATOR_GBA-EMU% %DATA%=%ROMPROVIDER%</command>\n"
	"        <command label=\"My Boy! (Standalone)\">%EMULATOR_MY-BOY% %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%</command>\n"
	"        <command label=\"Linkboy (Standalone)\">%EMULATOR_LINKBOY% %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%</command>\n"
	"        <command label=\"Pizza Boy GBA (Standalone)\">%EMULATOR_PIZZA-BOY-GBA% %ACTIVITY_CLEAR_TASK% %ACTIVITY_CLEAR_TOP% %EXTRA_rom_uri%=%ROMSAF%</command>\n"
#undef RA
	"        <platform>gba</platform>\n"
	"        <theme>gba</theme>\n";

#define RULE_OPEN "<emulator name=\"GBA-DS\">"
static const char* const RULE =
	"    " RULE_OPEN "\n"
	"        <!-- gba-ds: GBA DS, added by GBA DS (Settings > GBA DS in ES-DE); switching that off takes it out -->\n"
	"        <rule type=\"androidpackage\">\n"
	"            <entry>io.github.kyannon.gbads/.LauncherActivity</entry>\n"
	"        </rule>\n"
	"    </emulator>\n";

/* ------------------------------------------------------------ text helpers */

typedef struct {
	char* s;
	size_t n, cap;
} Buf;

static void put(Buf* b, const char* s, size_t n) {
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2;
		b->s = realloc(b->s, b->cap);
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void puts_(Buf* b, const char* s) {
	put(b, s, strlen(s));
}

/* The gba system, its RetroArch lines in SAF mode if asked (one string) */
static char* gba_system(bool saf) {
	Buf b = { 0 };
	puts_(&b, "    <system>\n");
	puts_(&b, COPY_NOTE);
	puts_(&b, "        <name>gba</name>\n");
	const char* p = GBA_BODY;
	const char* ra = "%EXTRA_ROM%=%ROM%</command>";
	for (const char* q; saf && (q = strstr(p, ra)); p = q + strlen(ra)) {
		put(&b, p, (size_t) (q - p));
		puts_(&b, "%EXTRA_ROM%=%ROMSAF%</command>");
	}
	puts_(&b, p);
	puts_(&b, "    </system>\n");
	return b.s;
}

/* A copy of xml with every comment blanked out (same length, same offsets),
 * to search in: a commented-out <system> or <command> doesn't count. */
static char* masked(const char* xml) {
	char* m = strdup(xml);
	for (char* p = strstr(m, "<!--"); p; p = strstr(p, "<!--")) {
		char* e = strstr(p + 4, "-->");
		char* end = e ? e + 3 : p + strlen(p);
		memset(p, ' ', (size_t) (end - p));
		p = end;
	}
	return m;
}

/* The start of the line p is on, and the start of the line after it */
static size_t line_start(const char* s, size_t at) {
	while (at > 0 && s[at - 1] != '\n') --at;
	return at;
}

static size_t line_after(const char* s, size_t at) {
	const char* nl = strchr(s + at, '\n');
	return nl ? (size_t) (nl - s) + 1 : strlen(s);
}

/* The element open...close whose child <tag> is name (or the first one
 * that starts with head, if name is NULL), as offsets in m: [*from, *to)
 * from the start of its first line to the line after its end. */
static bool find_elem(const char* m, const char* open, const char* close, const char* head, const char* name, size_t* from, size_t* to) {
	size_t ol = strlen(open);
	for (const char* p = strstr(m, open); p; p = strstr(p + 1, open)) {
		if (p[ol] != '>' && p[ol] != ' ' && p[ol] != '\t' && p[ol] != '\n' && p[ol] != '\r') continue;
		const char* e = strstr(p, close);
		if (!e) return false;
		bool hit;
		if (name) {
			const char* n = strstr(p, "<name>");
			hit = false;
			if (n && n < e) {
				n += 6;
				while (*n == ' ' || *n == '\t' || *n == '\n' || *n == '\r') ++n;
				size_t len = strlen(name);
				const char* after = n + len;
				while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') ++after;
				hit = !strncmp(n, name, len) && !strncmp(after, "</name>", 7);
			}
		} else {
			hit = !strncmp(p, head, strlen(head));
		}
		if (hit) {
			*from = line_start(m, (size_t) (p - m));
			*to = line_after(m, (size_t) (e - m) + strlen(close));
			return true;
		}
		p = e;
	}
	return false;
}

/* xml with [from, to) replaced by text */
static char* splice(const char* xml, size_t from, size_t to, const char* text) {
	Buf b = { 0 };
	put(&b, xml, from);
	puts_(&b, text);
	puts_(&b, xml + to);
	return b.s;
}

/* the result, or NULL if it's the same as before */
static char* changed(const char* before, char* after) {
	if (after && before && !strcmp(before, after)) {
		free(after);
		return NULL;
	}
	return after;
}

/* the start of the line holding the last close in m, or (size_t) -1 */
static size_t before_close(const char* m, const char* close) {
	const char* p = NULL;
	for (const char* q = strstr(m, close); q; q = strstr(q + 1, close)) p = q;
	return p ? line_start(m, (size_t) (p - m)) : (size_t) -1;
}

/* ------------------------------------------------------------ es_systems.xml */

char* esde_systems_add(const char* xml, bool saf) {
	char* sys = gba_system(saf);
	if (!xml || !xml[strspn(xml, " \t\r\n")]) {
		Buf b = { 0 };
		puts_(&b, "<?xml version=\"1.0\"?>\n"
		          "<!-- ES-DE's custom systems: these replace ES-DE's own systems of the same name -->\n"
		          "<systemList>\n");
		puts_(&b, sys);
		puts_(&b, "</systemList>\n");
		free(sys);
		return b.s;
	}
	char* m = masked(xml);
	char* out = NULL;
	size_t from, to;
	if (find_elem(m, "<system", "</system>", NULL, "gba", &from, &to)) {
		/* (the marker is in a comment: looked for in the text itself) */
		const char* mark = strstr(xml + from, COPY_MARK);
		if (mark && (size_t) (mark - xml) < to) {
			out = splice(xml, from, to, sys); /* GBA DS's copy: brought up to date */
		} else {
			/* the player's own gba entry: GBA DS's line first among its emulators */
			const char* own = strstr(m + from, CMD_OPEN);
			if (own && (size_t) (own - m) < to) {
				size_t a = line_start(m, (size_t) (own - m));
				out = splice(xml, a, line_after(m, a), CMD_LINE);
			} else {
				const char* cmd = strstr(m + from, "<command");
				const char* close = strstr(m + from, "</system>"); /* (this system's: find_elem found it) */
				size_t at = line_start(m, (size_t) ((cmd && (size_t) (cmd - m) < to ? cmd : close) - m));
				out = splice(xml, at, at, CMD_LINE);
			}
		}
	} else {
		size_t at = before_close(m, "</systemList>");
		if (at != (size_t) -1) out = splice(xml, at, at, sys);
	}
	free(m);
	free(sys);
	return changed(xml, out);
}

/* "" when nothing's left but the frame (the file can go) */
static char* empty_if_bare(char* out, const char* elem) {
	if (!out) return NULL;
	char* m = masked(out);
	bool any = false; /* (<system, not <systemList>) */
	size_t n = strlen(elem);
	for (const char* p = strstr(m, elem); p && !any; p = strstr(p + 1, elem)) any = p[n] == '>' || p[n] == ' ' || p[n] == '\t' || p[n] == '\n' || p[n] == '\r';
	free(m);
	if (!any) out[0] = 0;
	return out;
}

char* esde_systems_remove(const char* xml) {
	if (!xml) return NULL;
	char* m = masked(xml);
	char* out = NULL;
	size_t from, to;
	if (find_elem(m, "<system", "</system>", NULL, "gba", &from, &to)) {
		const char* mark = strstr(xml + from, COPY_MARK);
		if (mark && (size_t) (mark - xml) < to) {
			out = splice(xml, from, to, "");
		} else {
			const char* own = strstr(m + from, CMD_OPEN);
			if (own && (size_t) (own - m) < to) {
				size_t a = line_start(m, (size_t) (own - m));
				out = splice(xml, a, line_after(m, a), "");
			}
		}
	}
	free(m);
	return empty_if_bare(changed(xml, out), "<system");
}

/* ------------------------------------------------------------ es_find_rules.xml */

char* esde_rules_add(const char* xml) {
	if (!xml || !xml[strspn(xml, " \t\r\n")]) {
		Buf b = { 0 };
		puts_(&b, "<?xml version=\"1.0\"?>\n"
		          "<!-- ES-DE's custom find rules: where to find emulators ES-DE doesn't know -->\n"
		          "<ruleList>\n");
		puts_(&b, RULE);
		puts_(&b, "</ruleList>\n");
		return b.s;
	}
	char* m = masked(xml);
	char* out = NULL;
	size_t from, to;
	if (find_elem(m, "<emulator", "</emulator>", RULE_OPEN, NULL, &from, &to)) {
		out = splice(xml, from, to, RULE);
	} else {
		size_t at = before_close(m, "</ruleList>");
		if (at != (size_t) -1) out = splice(xml, at, at, RULE);
	}
	free(m);
	return changed(xml, out);
}

char* esde_rules_remove(const char* xml) {
	if (!xml) return NULL;
	char* m = masked(xml);
	char* out = NULL;
	size_t from, to;
	if (find_elem(m, "<emulator", "</emulator>", RULE_OPEN, NULL, &from, &to)) out = splice(xml, from, to, "");
	free(m);
	return empty_if_bare(changed(xml, out), "<emulator");
}

/* ------------------------------------------------------------ files */

static char esdeDir[512];

static bool is_esde(const char* dir) {
	char p[700];
	struct stat st;
	snprintf(p, sizeof(p), "%s/settings/es_settings.xml", dir);
	return stat(p, &st) == 0;
}

/* ES-DE's folder: ES-DE on the internal storage by default; the player may
 * have put it on an SD card. GBADS_ESDE_DIR overrides (tests). */
static const char* esde_dir(void) {
	const char* env = getenv("GBADS_ESDE_DIR");
	if (env && env[0]) return is_esde(env) ? env : NULL;
	if (esdeDir[0] && is_esde(esdeDir)) return esdeDir;
	esdeDir[0] = 0;
#ifdef __ANDROID__
	snprintf(esdeDir, sizeof(esdeDir), "/storage/emulated/0/ES-DE");
	if (is_esde(esdeDir)) return esdeDir;
	DIR* d = opendir("/storage");
	if (d) {
		struct dirent* e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.' || !strcmp(e->d_name, "emulated") || !strcmp(e->d_name, "self")) continue;
			snprintf(esdeDir, sizeof(esdeDir), "/storage/%s/ES-DE", e->d_name);
			if (is_esde(esdeDir)) break;
			esdeDir[0] = 0;
		}
		closedir(d);
	}
#endif
	if (esdeDir[0] && is_esde(esdeDir)) return esdeDir;
	esdeDir[0] = 0;
	return NULL;
}

static char* read_file(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = n >= 0 && n < (8 << 20) ? malloc((size_t) n + 1) : NULL;
	if (buf && fread(buf, 1, (size_t) n, f) == (size_t) n) {
		buf[n] = 0;
	} else {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

/* via a temporary file, so ES-DE never reads half a file */
static bool write_file(const char* path, const char* text) {
	char tmp[800];
	snprintf(tmp, sizeof(tmp), "%s.gba-ds.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f) return false;
	bool ok = fputs(text, f) >= 0;
	ok = fclose(f) == 0 && ok;
	if (ok && rename(tmp, path) != 0) ok = false;
	if (!ok) unlink(tmp);
	return ok;
}

/* ES-DE's "Use RetroArch in SAF mode" (es_settings.xml RetroArchSAFMode) */
static bool saf_mode(const char* dir) {
	char p[700];
	snprintf(p, sizeof(p), "%s/settings/es_settings.xml", dir);
	char* s = read_file(p);
	bool on = false;
	const char* k = s ? strstr(s, "\"RetroArchSAFMode\"") : NULL;
	if (k) {
		const char* end = strstr(k, "/>");
		const char* v = strstr(k, "value=\"true\"");
		on = v && (!end || v < end);
	}
	free(s);
	return on;
}

/* Applies an edit to one file in custom_systems; false if it couldn't be written */
static bool edit(const char* dir, const char* name, char* (*fn)(const char*, bool), bool arg) {
	char p[700];
	snprintf(p, sizeof(p), "%s/custom_systems", dir);
	mkdir(p, 0775);
	snprintf(p, sizeof(p), "%s/custom_systems/%s", dir, name);
	char* old = read_file(p);
	char* now = fn(old, arg);
	bool ok = true;
	if (now) {
		ok = now[0] ? write_file(p, now) : unlink(p) == 0;
		fprintf(stderr, "esde: %s %s%s\n", now[0] ? "wrote" : "removed", p, ok ? "" : " (failed)");
	}
	free(old);
	free(now);
	return ok;
}

static char* sys_add(const char* x, bool saf) { return esde_systems_add(x, saf); }
static char* sys_remove(const char* x, bool unused) { (void) unused; return esde_systems_remove(x); }
static char* rules_add(const char* x, bool unused) { (void) unused; return esde_rules_add(x); }
static char* rules_remove(const char* x, bool unused) { (void) unused; return esde_rules_remove(x); }

bool esde_present(void) {
	return esde_dir() != NULL;
}

bool esde_integration_on(void) {
	const char* dir = esde_dir();
	if (!dir) return false;
	char p[700];
	snprintf(p, sizeof(p), "%s/custom_systems/es_systems.xml", dir);
	char* s = read_file(p);
	if (!s) return false;
	char* m = masked(s);
	size_t from, to;
	bool on = false;
	if (find_elem(m, "<system", "</system>", NULL, "gba", &from, &to)) {
		const char* c = strstr(m + from, CMD_OPEN);
		on = c && (size_t) (c - m) < to;
	}
	free(m);
	free(s);
	return on;
}

bool esde_integration_set(bool on, char* msg, size_t msgSize) {
	const char* dir = esde_dir();
	if (!dir) {
		snprintf(msg, msgSize, "ES-DE's folder wasn't found.");
		return false;
	}
	bool ok;
	if (on) {
		bool saf = saf_mode(dir);
		ok = edit(dir, "es_find_rules.xml", rules_add, false) && edit(dir, "es_systems.xml", sys_add, saf);
		if (!ok) edit(dir, "es_find_rules.xml", rules_remove, false);
	} else {
		ok = edit(dir, "es_systems.xml", sys_remove, false);
		if (ok) edit(dir, "es_find_rules.xml", rules_remove, false);
	}
	if (!ok)
		snprintf(msg, msgSize, tr("Couldn't change the files in %s/custom_systems: allow GBA DS All files access in Android's settings."), dir);
	else if (on)
		snprintf(msg, msgSize, "GBA DS is now ES-DE's emulator for Game Boy Advance games. Quit ES-DE and open it again to see it.");
	else
		snprintf(msg, msgSize, "GBA DS is out of ES-DE's emulator list. Quit ES-DE and open it again to see it.");
	return ok;
}

void esde_integration_refresh(void) {
	if (!esde_integration_on()) return;
	const char* dir = esde_dir();
	edit(dir, "es_find_rules.xml", rules_add, false);
	edit(dir, "es_systems.xml", sys_add, saf_mode(dir));
}
