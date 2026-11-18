/* The interface's language (the Chinese translation, i18n_zh.c).
 *
 * English is what the code says; every string the player can read is passed
 * through tr() on its way to the screen (ui.c), and the ones the code builds
 * itself (a number in a sentence, a value in a row) through tr() where they
 * are built. tr() returns the same string when there's no translation for
 * it, so a part that isn't translated yet reads in English.
 *
 * The translations live in assets/i18n/zh-CN.tsv, one "English<TAB>Chinese"
 * line each, written to src/i18n_zh.c by tools/make_i18n.py. */
#ifndef GBADS_I18N_H
#define GBADS_I18N_H

#include <stdbool.h>

enum { LANG_EN, LANG_ZH, LANG_ZT, LANG_COUNT };

typedef struct {
	const char* en;
	const char* zh;
} I18nEntry;

/* The language's name as gba-ds.cfg says it: "auto" (the system's), "en",
 * "zh" (Simplified) or "zh-tw" (Traditional). Anything else is English. */
void i18n_init(const char* language);
void i18n_set(const char* language); /* the setting changed: from the next frame on */
const char* i18n_language(void);     /* "en", "zh" or "zh-tw" (auto worked out) */
const char* i18n_setting(void);      /* "auto", "en", "zh" or "zh-tw": as it's kept */
int i18n_lang(void);                 /* LANG_EN, LANG_ZH or LANG_ZT */

/* The translation of en, or en itself (NULL for NULL). */
const char* tr(const char* en);
/* English's plural ending for a count: "s", or nothing in Chinese, for
 * "Found %d game%s" (a translated line can then say it without one). */
const char* i18n_plural(int n);

/* For the tests and tools: the table itself. */
int i18n_entry_count(void);
const char* i18n_entry_en(int i);
const char* i18n_entry_zh(int i);
/* en is in the table (the tests: every string a screen can show is). */
bool i18n_known(const char* en);

#endif
