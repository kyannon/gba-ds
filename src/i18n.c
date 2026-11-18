/* The interface's language: the tables of translations (i18n_zh.c and
 * i18n_zt.c, made by tools/make_i18n.py from assets/i18n/) and tr(). */
#include "i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i18n_zh.h"
#include "i18n_zt.h"

static int lang = LANG_EN;
static char setting[12] = "auto";

/* (the tables are written in strcmp order by tools/make_i18n.py: bsearch
 * needs nothing sorted here) */
static void tables_init(void) {
}

/* bsearch's comparator: key is a pointer to the English string */
static int key_cmp(const void* key, const void* b) {
	return strcmp(*(const char* const*) key, ((const I18nEntry*) b)->en);
}

static const I18nEntry* table_for(int language, int* count) {
	if (language == LANG_ZH) {
		tables_init();
		*count = i18nZhCount;
		return i18nZhTable;
	}
	if (language == LANG_ZT) {
		tables_init();
		*count = i18nZtCount;
		return i18nZtTable;
	}
	*count = 0;
	return NULL;
}

static int language_of(const char* name) {
	if (!name) return LANG_EN;
	if (!strcasecmp(name, "zh-tw") || !strcasecmp(name, "zh_tw") || !strcasecmp(name, "zh-hant") ||
	    !strcasecmp(name, "zh-hk") || !strcasecmp(name, "zh-mo") || !strcasecmp(name, "tw") ||
	    !strcasecmp(name, "traditional") || !strcasecmp(name, "traditional chinese"))
		return LANG_ZT;
	if (!strcasecmp(name, "zh") || !strcasecmp(name, "zh-cn") || !strcasecmp(name, "zh_cn") ||
	    !strcasecmp(name, "zh-hans") || !strcasecmp(name, "zh-sg") || !strcasecmp(name, "cn") ||
	    !strcasecmp(name, "chinese") || !strcasecmp(name, "simplified chinese"))
		return LANG_ZH;
	return LANG_EN;
}

/* word (lower case) anywhere in haystack, whatever its case */
static bool has_word(const char* haystack, const char* word) {
	size_t n = strlen(word);
	for (const char* p = haystack; *p; ++p) {
		size_t i = 0;
		while (i < n && p[i] && tolower((unsigned char) p[i]) == (unsigned char) word[i]) ++i;
		if (i == n) return true;
	}
	return false;
}

/* "zh_TW.UTF-8", "zh-Hant-TW", "zh_CN": which of Chinese's */
static int language_of_locale(const char* v) {
	if (!v || v[0] != 'z' || v[1] != 'h') return LANG_EN;
	if (has_word(v, "hant") || has_word(v, "_tw") || has_word(v, "-tw") || has_word(v, "_hk") ||
	    has_word(v, "-hk") || has_word(v, "_mo") || has_word(v, "-mo"))
		return LANG_ZT;
	return LANG_ZH;
}

/* "auto": the system's language (`LANG`, `LC_ALL`, `LC_MESSAGES`), Chinese
 * when it's one of Chinese's */
static int system_language(void) {
	static const char* const names[] = { "LC_ALL", "LC_MESSAGES", "LANG", NULL };
	for (int i = 0; names[i]; ++i) {
		const char* v = getenv(names[i]);
		if (v && v[0]) return language_of_locale(v);
	}
	return LANG_EN;
}

void i18n_init(const char* language) {
	i18n_set(language);
	tables_init();
}

void i18n_set(const char* language) {
	snprintf(setting, sizeof(setting), "%s", language && language[0] ? language : "auto");
	lang = !strcmp(setting, "auto") ? system_language() : language_of(setting);
}

const char* i18n_language(void) {
	return lang == LANG_ZH ? "zh" : lang == LANG_ZT ? "zh-tw" : "en";
}

const char* i18n_setting(void) {
	return setting;
}

int i18n_lang(void) {
	return lang;
}

const char* tr(const char* en) {
	if (!en || !en[0] || lang == LANG_EN) return en;
	int count = 0;
	const I18nEntry* table = table_for(lang, &count);
	if (!table || !count) return en;
	const I18nEntry* e = bsearch(&en, table, (size_t) count, sizeof(table[0]), key_cmp);
	return e ? e->zh : en;
}

const char* i18n_plural(int n) {
	(void) n;
	return lang == LANG_EN ? "s" : "";
}

int i18n_entry_count(void) {
	int count = 0;
	table_for(lang, &count);
	return count;
}

const char* i18n_entry_en(int i) {
	int count = 0;
	const I18nEntry* table = table_for(lang, &count);
	if (!table || i < 0 || i >= count) return NULL;
	return table[i].en;
}

const char* i18n_entry_zh(int i) {
	int count = 0;
	const I18nEntry* table = table_for(lang, &count);
	if (!table || i < 0 || i >= count) return NULL;
	return table[i].zh;
}

bool i18n_known(const char* en) {
	if (!en) return false;
	int count = 0;
	const I18nEntry* table = table_for(lang, &count);
	if (!table || !count) return false;
	return bsearch(&en, table, (size_t) count, sizeof(table[0]), key_cmp) != NULL;
}
