/* The interface's language (docs/I18N.md).
 *
 * English is what the code says. A string the player can read is looked up
 * by its English text (and, where one word means two things, a context) in
 * the language's table, which tools/i18n.py compiles from the .po files in assets/i18n
 * into src/i18n_data.c. A string with no translation reads in English, so a
 * language that is only partly done is still usable.
 *
 *   tr("Cheats")                         the text, drawn by ui_label / ui_font_text
 *   trc("direction", "Left")             with a context (msgctxt)
 *   trn("%d game", "%d games", n)        the format for n (the language's plural rule)
 *   N_("Cheats")                         marks a string in a table; tr() it where it's drawn
 *
 * Formats may reorder their arguments (%2$s ... %1$s) as long as they use all
 * of English's, with the same types: tools/i18n.py check says if not. */
#ifndef GBADS_I18N_H
#define GBADS_I18N_H

#include <stdbool.h>

/* how a language says one, few, many: the index into a plural entry's forms */
enum { PL_NONE, PL_ONE_OTHER, PL_FRENCH, PL_EAST_SLAVIC, PL_POLISH, PL_CZECH };
/* the script a language is drawn in: which fallback fonts it needs (textfont.h) */
enum { SCRIPT_LATIN, SCRIPT_HAN_SC, SCRIPT_HAN_TC, SCRIPT_JP, SCRIPT_KO, SCRIPT_COUNT }; /* (the order of SCRIPTS in tools/i18n.py) */
/* how far a catalogue got: machine-made, checked by a person, or for testing */
enum { I18N_UNKNOWN, I18N_MACHINE, I18N_REVIEWED, I18N_TEST };

typedef struct {
	const char* key;              /* "context\4English", or just the English */
	const char* const* forms;     /* one, or one per plural form */
	unsigned char n;              /* how many */
} I18nEntry;

typedef struct {
	const char* code;             /* "de", "zh_CN": the .po file's name; also the setting's value */
	const char* autonym;          /* its own name, as Settings lists it */
	const char* locales;          /* other system locales that mean it, space separated */
	int plural, script, status;
	const I18nEntry* entries;     /* sorted by key (strcmp) */
	int count;
} I18nLang;

/* made by tools/i18n.py compile, sorted by code */
extern const I18nLang i18nLangs[];
extern const int i18nLangCount;

/* The setting: "auto" (the system's language), "en", or a language's code.
 * Anything else, or a language the build doesn't have, is English. */
void i18n_init(const char* setting);
void i18n_set(const char* setting); /* the setting changed: from the next string on */
const char* i18n_setting(void);     /* as it was set ("auto" worked out by i18n_code) */
const char* i18n_code(void);        /* the language in use: "en" or a code */
const I18nLang* i18n_lang(void);    /* NULL for English */
int i18n_script(void);              /* SCRIPT_* of the language in use */

/* format_arg: the result is a format string made from the argument, so
 * snprintf(buf, n, tr("No formats here")) is checked like a literal (Android
 * builds with -Werror=format-security) and tr("%d files") against its
 * arguments, as gettext's _() is. */
#if defined(__GNUC__) || defined(__clang__)
#define I18N_FORMAT_ARG(...) __attribute__((format_arg(__VA_ARGS__)))
#else
#define I18N_FORMAT_ARG(...)
#endif
const char* tr(const char* en) I18N_FORMAT_ARG(1);
const char* trc(const char* ctx, const char* en) I18N_FORMAT_ARG(2);
const char* trn(const char* one, const char* other, long n) I18N_FORMAT_ARG(1) I18N_FORMAT_ARG(2);
const char* trnc(const char* ctx, const char* one, const char* other, long n);

/* markers for tools/i18n.py: the string is translated where it is drawn */
#define N_(s) (s)
#define NC_(ctx, s) (s)

/* the system's language as one of the build's (i18nLangs[]; -1: none, which is English) */
int i18n_system_index(void);
/* en as a given language says it (the first form), whatever language is in use: the first-run
 * language screen writes each language's name and note in that language. code: "en" or a code. */
const char* i18n_tr_in(const char* code, const char* en);

/* the system's language as one of the build's (-1: none), from its locale
 * name ("de_DE.UTF-8", "zh-Hant-TW"); for the tests and `auto` */
int i18n_match_locale(const char* locale);
/* the plural form n takes in a language with this rule (0 for none) */
int i18n_plural_index(int rule, long n);

#endif
