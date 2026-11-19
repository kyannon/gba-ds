/* Tests for src/i18n.c (which language, lookups, plurals) and src/textfont.c's
 * fallback faces. Own small tables stand in for the generated ones.
 *   make test-i18n        (runs it; needs DejaVu Sans for the font part, or skips it)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/i18n.h"
#include "../src/textfont.h"

static int fails;
#define CHECK(cond)                                                                                                    \
	do {                                                                                                           \
		if (!(cond)) {                                                                                         \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                         \
			++fails;                                                                                       \
		}                                                                                                      \
	} while (0)
#define CHECK_STR(a, b)                                                                                                \
	do {                                                                                                           \
		const char* _a = (a);                                                                                  \
		const char* _b = (b);                                                                                  \
		if (!_a || !_b || strcmp(_a, _b)) {                                                                    \
			printf("FAIL %s:%d: %s = \"%s\", wanted \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b); \
			++fails;                                                                                       \
		}                                                                                                      \
	} while (0)

/* sorted by key (strcmp) */
static const char* const de_back[] = { "Zur\303\274ck" };
static const char* const de_left[] = { "Links" };
static const char* const de_games[] = { "%d Spiel", "%d Spiele" };
static const char* const de_slot[] = { "Auf %2$s: %1$s" };
static const I18nEntry deEntries[] = {
	{ "%d game", de_games, 2 },
	{ "%s on %s", de_slot, 1 },
	{ "direction\4Left", de_left, 1 },
	{ "menu\4Back", de_back, 1 },
};
static const char* const ru_games[] = { "%d \320\270\320\263\321\200\320\260", "%d \320\270\320\263\321\200\321\213", "%d \320\270\320\263\321\200" };
static const I18nEntry ruEntries[] = { { "%d game", ru_games, 3 } };
static const char* const ja_games[] = { "%d\345\200\213" };
static const I18nEntry jaEntries[] = { { "%d game", ja_games, 1 } };
static const char* const fr_games[] = { "%d jeu", "%d jeux" };
static const I18nEntry frEntries[] = { { "%d game", fr_games, 2 } };
static const char* const ps_ok[] = { "\303\226k" };
static const I18nEntry psEntries[] = { { "OK", ps_ok, 1 } };

const I18nLang i18nLangs[] = {
	{ "de", "Deutsch", "de_AT de_CH", PL_ONE_OTHER, SCRIPT_LATIN, I18N_MACHINE, deEntries, 4 },
	{ "fr", "Fran\303\247ais", "", PL_FRENCH, SCRIPT_LATIN, I18N_REVIEWED, frEntries, 1 },
	{ "ja", "\346\227\245\346\234\254\350\252\236", "", PL_NONE, SCRIPT_LATIN, I18N_MACHINE, jaEntries, 1 },
	{ "pseudo", "Pseudo", "", PL_ONE_OTHER, SCRIPT_LATIN, I18N_TEST, psEntries, 1 },
	{ "ru", "\320\240\321\203\321\201\321\201\320\272\320\270\320\271", "", PL_EAST_SLAVIC, SCRIPT_LATIN, I18N_MACHINE, ruEntries, 1 },
	{ "zh_CN", "\344\270\255\346\226\207", "zh_Hans zh_SG", PL_NONE, SCRIPT_LATIN, I18N_MACHINE, jaEntries, 1 },
	{ "zh_TW", "\344\270\255\346\226\207", "zh_Hant zh_HK zh_MO", PL_NONE, SCRIPT_LATIN, I18N_MACHINE, jaEntries, 1 },
};
const int i18nLangCount = sizeof(i18nLangs) / sizeof(i18nLangs[0]);

static const char* code_of(int i) {
	return i < 0 ? "-" : i18nLangs[i].code;
}

static void test_locales(void) {
	CHECK_STR(code_of(i18n_match_locale("de_DE.UTF-8")), "de");
	CHECK_STR(code_of(i18n_match_locale("de-AT")), "de");
	CHECK_STR(code_of(i18n_match_locale("de_CH@euro")), "de");
	CHECK_STR(code_of(i18n_match_locale("DE")), "de");
	CHECK_STR(code_of(i18n_match_locale("fr_CA.UTF-8")), "fr");
	CHECK_STR(code_of(i18n_match_locale("zh_CN.UTF-8")), "zh_CN");
	CHECK_STR(code_of(i18n_match_locale("zh_TW.UTF-8")), "zh_TW");
	CHECK_STR(code_of(i18n_match_locale("zh_HK.UTF-8")), "zh_TW");
	CHECK_STR(code_of(i18n_match_locale("zh-Hant-TW")), "zh_TW"); /* zh_Hant_TW -> zh_TW is exact first */
	CHECK_STR(code_of(i18n_match_locale("zh-Hant-HK")), "zh_TW"); /* then zh_Hant */
	CHECK_STR(code_of(i18n_match_locale("zh-Hans-CN")), "zh_CN");
	CHECK_STR(code_of(i18n_match_locale("zh")), "zh_CN");         /* a bare language: the first of its base */
	CHECK_STR(code_of(i18n_match_locale("zh_SG")), "zh_CN");
	CHECK_STR(code_of(i18n_match_locale("en_US.UTF-8")), "-");
	CHECK_STR(code_of(i18n_match_locale("C")), "-");
	CHECK_STR(code_of(i18n_match_locale("POSIX")), "-");
	CHECK_STR(code_of(i18n_match_locale("")), "-");
	CHECK_STR(code_of(i18n_match_locale(NULL)), "-");
	CHECK_STR(code_of(i18n_match_locale("pseudo")), "-"); /* test languages are never the system's */
	CHECK_STR(code_of(i18n_match_locale("xx_YY")), "-");
	CHECK_STR(code_of(i18n_match_locale("d")), "-");
}

static void test_lookup(void) {
	i18n_set("en");
	CHECK_STR(i18n_code(), "en");
	CHECK_STR(tr("Cheats"), "Cheats");
	CHECK_STR(trn("%d game", "%d games", 1), "%d game");
	CHECK_STR(trn("%d game", "%d games", 2), "%d games");
	CHECK(tr(NULL) == NULL);

	i18n_set("de");
	CHECK_STR(i18n_code(), "de");
	CHECK_STR(tr("Cheats"), "Cheats");                        /* no entry: English */
	CHECK_STR(trc("menu", "Back"), "Zur\303\274ck");
	CHECK_STR(trc("direction", "Left"), "Links");
	CHECK_STR(trc("other", "Left"), "Left");                  /* the context matters */
	CHECK_STR(tr("Left"), "Left");
	CHECK_STR(tr("%s on %s"), "Auf %2$s: %1$s");
	CHECK_STR(trn("%d game", "%d games", 1), "%d Spiel");
	CHECK_STR(trn("%d game", "%d games", 0), "%d Spiele");
	CHECK_STR(trn("%d game", "%d games", 5), "%d Spiele");
	CHECK(i18n_script() == SCRIPT_LATIN);
	char buf[64];
	snprintf(buf, sizeof(buf), tr("%s on %s"), "A", "B"); /* reordered arguments */
	CHECK_STR(buf, "Auf B: A");

	i18n_set("ru");
	CHECK_STR(trn("%d game", "%d games", 1), ru_games[0]);
	CHECK_STR(trn("%d game", "%d games", 21), ru_games[0]);
	CHECK_STR(trn("%d game", "%d games", 3), ru_games[1]);
	CHECK_STR(trn("%d game", "%d games", 24), ru_games[1]);
	CHECK_STR(trn("%d game", "%d games", 5), ru_games[2]);
	CHECK_STR(trn("%d game", "%d games", 11), ru_games[2]);
	CHECK_STR(trn("%d game", "%d games", 112), ru_games[2]);
	CHECK_STR(trn("%d game", "%d games", 0), ru_games[2]);

	i18n_set("fr");
	CHECK_STR(trn("%d game", "%d games", 0), "%d jeu");
	CHECK_STR(trn("%d game", "%d games", 1), "%d jeu");
	CHECK_STR(trn("%d game", "%d games", 2), "%d jeux");
	i18n_set("ja");
	CHECK_STR(trn("%d game", "%d games", 1), ja_games[0]);
	CHECK_STR(trn("%d game", "%d games", 7), ja_games[0]);

	i18n_set("pseudo"); /* chosen by name works, though `auto` never picks it */
	CHECK_STR(tr("OK"), "\303\226k");

	i18n_set("klingon"); /* a language the build doesn't have: English */
	CHECK_STR(i18n_code(), "en");
	CHECK_STR(i18n_setting(), "klingon");
	CHECK(i18n_lang() == NULL);
	CHECK_STR(tr("OK"), "OK");
}

static void test_auto(void) {
	unsetenv("LC_ALL"), unsetenv("LC_MESSAGES"), unsetenv("LANGUAGE"), unsetenv("LANG");
	i18n_set("auto");
	CHECK_STR(i18n_code(), "en");
	setenv("LANG", "de_DE.UTF-8", 1);
	i18n_set("auto");
	CHECK_STR(i18n_code(), "de");
	setenv("LC_ALL", "fr_FR.UTF-8", 1); /* LC_ALL beats LANG */
	i18n_set("auto");
	CHECK_STR(i18n_code(), "fr");
	setenv("LC_ALL", "C", 1); /* "C" is English even with LANG set: that's what the system says */
	i18n_set("auto");
	CHECK_STR(i18n_code(), "en");
	unsetenv("LC_ALL");
	setenv("LANGUAGE", "xx:de", 1); /* LANGUAGE's list: the first we have */
	i18n_set("auto");
	CHECK_STR(i18n_code(), "de");
	setenv("LANGUAGE", "en:de", 1); /* English first means English */
	i18n_set("auto");
	CHECK_STR(i18n_code(), "en");
	unsetenv("LANGUAGE");
	unsetenv("LANG");
	i18n_set(NULL);
	CHECK_STR(i18n_setting(), "auto");
}

static unsigned char* read_file(const char* path, long* n) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* b = malloc((size_t) *n);
	if (b && fread(b, 1, (size_t) *n, f) != (size_t) *n) b = NULL;
	fclose(f);
	return b;
}

static int ink(const TfBitmap* b) {
	int n = 0;
	for (int i = 0; i < b->w * b->h; ++i) n += b->alpha[i] > 40;
	return n;
}

static void test_fallback(const char* ttfPath) {
	long n;
	unsigned char* ttf = ttfPath ? read_file(ttfPath, &n) : NULL;
	if (!ttf) {
		printf("skipped the font fallback test: no font file (pass DejaVuSans.ttf)\n");
		return;
	}
	const char* greek = "\316\223\316\265\316\271\316\254"; /* Γειά: Jost has no Greek */
	const char* latin = "Pok\303\251mon Left 12";
	tf_fallback_clear();
	TfBitmap latinBefore, greekBefore, greekAfter, latinAfter, mixed;
	const char* iota = "\316\271"; /* narrow in DejaVu, a full-width .notdef box in Jost */
	int wGreekBefore = tf_width(TF_BODY, 40, greek), wIotaBefore = tf_width(TF_BODY, 40, iota);
	CHECK(tf_render(TF_BODY, 40, latin, &latinBefore));
	CHECK(tf_render(TF_BODY, 40, greek, &greekBefore));

	CHECK(tf_fallback_add(TF_BODY, ttf));
	CHECK(tf_fallback_add(TF_BOLD, ttf));
	int wGreekAfter = tf_width(TF_BODY, 40, greek);
	CHECK(wGreekAfter > 0 && tf_width(TF_BODY, 40, iota) != wIotaBefore); /* drawn by DejaVu now, not Jost's .notdef */
	CHECK(tf_render(TF_BODY, 40, greek, &greekAfter));
	CHECK(ink(&greekAfter) > 0 && ink(&greekAfter) != ink(&greekBefore));
	/* Latin text, accents included, stays in Jost: the same pixels as before */
	CHECK(tf_render(TF_BODY, 40, latin, &latinAfter));
	CHECK(latinAfter.w == latinBefore.w && latinAfter.h == latinBefore.h && latinAfter.baseline == latinBefore.baseline);
	CHECK(!memcmp(latinAfter.alpha, latinBefore.alpha, (size_t) latinBefore.w * latinBefore.h));
	/* mixed text: widths add up, one baseline, rows for both */
	CHECK(tf_render(TF_BODY, 40, "Left \316\223\316\265\316\271\316\254", &mixed));
	CHECK(tf_width(TF_BODY, 40, "Left \316\223\316\265\316\271\316\254") == tf_width(TF_BODY, 40, "Left ") + wGreekAfter ||
	      abs(tf_width(TF_BODY, 40, "Left \316\223\316\265\316\271\316\254") - tf_width(TF_BODY, 40, "Left ") - wGreekAfter) <= 2);
	CHECK(mixed.baseline >= latinAfter.baseline);

	tf_fallback_clear(); /* back to Jost alone */
	CHECK(tf_width(TF_BODY, 40, greek) == wGreekBefore);
	/* a fallback past the limit is refused, not a crash */
	int added = 0;
	for (int i = 0; i < 6; ++i) added += tf_fallback_add(TF_BODY, ttf);
	CHECK(added == 5); /* (CHAIN_MAX 6: Jost and five) */
	tf_fallback_clear();
	CHECK(!tf_fallback_add(TF_BODY, (const unsigned char*) "not a font"));
	tf_free(&latinBefore), tf_free(&greekBefore), tf_free(&greekAfter), tf_free(&latinAfter), tf_free(&mixed);
}

/* Players' own game and folder names, in any script: every character has a glyph whatever the interface's
 * language is (the bold faces carry GB2312's and Big5's whole sets), so none draws as a box. */
static void test_names(void) {
	static const char* const names[] = {
		"\347\201\253\347\204\260\347\272\271\347\253\240 \351\223\240\347\224\262\347\232\204\345\221\274\345\224\244", /* 火焰纹章 铠甲的呼唤 (Simplified) */
		"F-Zero \345\267\205\345\263\260\344\271\213\346\210\260", /* F-Zero 巅峰之战 (巅 is not in the catalogue) */
		"\351\230\277\346\213\211\344\270\201", /* 阿拉丁 */
		"\351\216\247\347\224\262\347\232\204\345\221\274\345\226\232", /* 鎧甲的呼喚 (Traditional) */
		"\343\203\235\343\202\261\343\203\242\343\203\263 \343\201\202\343\201\204", /* ポケモン あい (kana) */
		"\355\225\234\352\265\255\354\226\264", /* 한국어 */
	};
	static const int scripts[] = { SCRIPT_LATIN, SCRIPT_HAN_SC, SCRIPT_HAN_TC, SCRIPT_JP, SCRIPT_KO };
	for (size_t si = 0; si < sizeof(scripts) / sizeof(scripts[0]); ++si) {
		tf_fallback_for_script(scripts[si]);
		for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n) {
			int missing = 0;
			for (const char* p = names[n]; *p;) {
				unsigned char c = (unsigned char) *p;
				int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
				unsigned cp = len == 1 ? c : c & (0x7F >> len);
				for (int k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char) p[k] & 0x3F);
				p += len;
				if (!tf_has_codepoint(TF_BOLD, cp)) ++missing;
			}
			if (missing) printf("script %d, name %zu: %d characters would be boxes\n", scripts[si], n, missing);
			CHECK(missing == 0);
		}
	}
	tf_fallback_clear();
	CHECK(!tf_has_codepoint(TF_BOLD, 0x5DC5)); /* 巅 with Jost alone: a box */
}

int main(int argc, char** argv) {
	test_locales();
	test_lookup();
	test_auto();
	test_fallback(argc > 1 ? argv[1] : NULL);
	test_names();
	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
