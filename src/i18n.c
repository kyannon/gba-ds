/* The interface's language: looking a string up in the tables that
 * tools/i18n.py compiled (i18n_data.c), the plural rules, and which language
 * `auto` means. */
#include "i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "i18n_data.h"

#ifdef __ANDROID__
#include <SDL.h>
#endif

static const I18nLang* cur = NULL; /* NULL: English */
static char setting[16] = "auto";

/* ---- which language ---- */

/* "de_DE.UTF-8@euro", "zh-Hant-TW" -> "de_DE", "zh_Hant_TW" (at most n-1 bytes) */
static void locale_norm(const char* in, char* out, size_t n) {
	size_t i = 0;
	for (; in[i] && in[i] != '.' && in[i] != '@' && i + 1 < n; ++i) out[i] = in[i] == '-' ? '_' : in[i];
	out[i] = 0;
}

static bool word_eq(const char* list, const char* name) {
	size_t n = strlen(name);
	for (const char* p = list; *p;) {
		while (*p == ' ') ++p;
		const char* e = p;
		while (*e && *e != ' ') ++e;
		if ((size_t) (e - p) == n && !strncasecmp(p, name, n)) return true;
		p = e;
	}
	return false;
}

static int find_code(const char* code) {
	for (int i = 0; i < i18nLangCount; ++i)
		if (!strcasecmp(i18nLangs[i].code, code)) return i;
	return -1;
}

int i18n_match_locale(const char* locale) {
	char name[48];
	if (!locale || !locale[0] || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) return -1;
	locale_norm(locale, name, sizeof(name));
	/* the whole name, then with its last part cut off each time: zh_Hant_TW, zh_Hant, zh */
	for (;;) {
		for (int i = 0; i < i18nLangCount; ++i) {
			if (i18nLangs[i].status == I18N_TEST) continue;
			if (!strcasecmp(i18nLangs[i].code, name) || word_eq(i18nLangs[i].locales, name)) return i;
		}
		char* us = strrchr(name, '_');
		if (!us) break;
		*us = 0;
	}
	/* any language of the same base (es_MX -> es, when only es_ES is there) */
	for (int i = 0; i < i18nLangCount; ++i) {
		if (i18nLangs[i].status == I18N_TEST) continue;
		const char* c = i18nLangs[i].code;
		size_t b = strcspn(c, "_");
		if (strlen(name) == b && !strncasecmp(c, name, b)) return i;
	}
	return -1;
}

static int system_language(void) {
	static const char* const vars[] = { "LC_ALL", "LC_MESSAGES", "LANG", NULL };
#ifdef __ANDROID__
	SDL_Locale* loc = SDL_GetPreferredLocales();
	if (loc) {
		int found = -1;
		for (int i = 0; loc[i].language && found < 0; ++i) {
			char name[48];
			snprintf(name, sizeof(name), "%s%s%s", loc[i].language, loc[i].country ? "_" : "", loc[i].country ? loc[i].country : "");
			found = i18n_match_locale(name);
		}
		SDL_free(loc);
		if (found >= 0) return found;
	}
#endif
	/* LANGUAGE holds a list ("de:en"), the others one locale */
	const char* list = getenv("LANGUAGE");
	if (list && list[0]) {
		char one[48];
		for (const char* p = list; *p;) {
			size_t n = strcspn(p, ":");
			snprintf(one, sizeof(one), "%.*s", (int) (n < sizeof(one) - 1 ? n : sizeof(one) - 1), p);
			int i = i18n_match_locale(one);
			if (i >= 0) return i;
			if (!strcmp(one, "en") || !strncmp(one, "en_", 3)) return -1;
			p += n + (p[n] == ':');
		}
	}
	for (int i = 0; vars[i]; ++i) {
		const char* v = getenv(vars[i]);
		if (v && v[0]) return i18n_match_locale(v);
	}
	return -1;
}

int i18n_system_index(void) {
	return system_language();
}

void i18n_set(const char* s) {
	snprintf(setting, sizeof(setting), "%s", s && s[0] ? s : "auto");
	int i = !strcmp(setting, "auto") ? system_language() : find_code(setting);
	cur = i >= 0 ? &i18nLangs[i] : NULL;
}

void i18n_init(const char* s) {
	i18n_set(s);
}

const char* i18n_setting(void) {
	return setting;
}

const char* i18n_code(void) {
	return cur ? cur->code : "en";
}

const I18nLang* i18n_lang(void) {
	return cur;
}

int i18n_script(void) {
	return cur ? cur->script : SCRIPT_LATIN;
}

/* ---- looking up ---- */

static int key_cmp(const void* key, const void* b) {
	return strcmp((const char*) key, ((const I18nEntry*) b)->key);
}

static const I18nEntry* find(const char* key) {
	if (!cur || !cur->count) return NULL;
	return bsearch(key, cur->entries, (size_t) cur->count, sizeof(cur->entries[0]), key_cmp);
}

static const I18nEntry* find_ctx(const char* ctx, const char* en) {
	char key[512];
	if (!ctx || !ctx[0]) return find(en);
	if (strlen(ctx) + strlen(en) + 2 > sizeof(key)) return NULL;
	snprintf(key, sizeof(key), "%s\4%s", ctx, en);
	return find(key);
}

const char* tr(const char* en) {
	if (!en || !cur) return en;
	const I18nEntry* e = find(en);
	return e ? e->forms[0] : en;
}

const char* trc(const char* ctx, const char* en) {
	if (!en || !cur) return en;
	const I18nEntry* e = find_ctx(ctx, en);
	return e ? e->forms[0] : en;
}

const char* i18n_tr_in(const char* code, const char* en) {
	int i = code ? find_code(code) : -1;
	if (!en || i < 0 || !i18nLangs[i].count) return en;
	const I18nEntry* e = bsearch(en, i18nLangs[i].entries, (size_t) i18nLangs[i].count, sizeof(i18nLangs[i].entries[0]), key_cmp);
	return e ? e->forms[0] : en;
}

int i18n_plural_index(int rule, long n) {
	if (n < 0) n = -n;
	long m10 = n % 10, m100 = n % 100;
	switch (rule) {
	case PL_ONE_OTHER: return n != 1;
	case PL_FRENCH: return n > 1;
	case PL_EAST_SLAVIC: return m10 == 1 && m100 != 11 ? 0 : m10 >= 2 && m10 <= 4 && (m100 < 10 || m100 >= 20) ? 1 : 2;
	case PL_POLISH: return n == 1 ? 0 : m10 >= 2 && m10 <= 4 && (m100 < 10 || m100 >= 20) ? 1 : 2;
	case PL_CZECH: return n == 1 ? 0 : n >= 2 && n <= 4 ? 1 : 2;
	default: return 0;
	}
}

const char* trnc(const char* ctx, const char* one, const char* other, long n) {
	const I18nEntry* e = cur ? find_ctx(ctx, one) : NULL;
	if (e) {
		int i = i18n_plural_index(cur->plural, n);
		return e->forms[i < e->n ? i : e->n - 1];
	}
	return n == 1 || n == -1 ? one : other;
}

const char* trn(const char* one, const char* other, long n) {
	return trnc(NULL, one, other, n);
}
