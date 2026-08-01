/*
 * Runtime interface localisation for the GTK front end. See i18n.h.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "i18n.h"

struct i18n_pair {
    const char *msgid;
    const char *msgstr;
};

/* Local patch: only include (and reference below) a table for a
 * language once po/xx.po has real translations and has been run
 * through po2c.py -- i18n_translate() treats a language with no entry
 * in i18n_tables[] as "fall back to English", same as English itself,
 * so it's always safe for a language to be missing here while its
 * translation work is still in progress. */
#include "i18n_table_ru.h"
#include "i18n_table_es.h"
#include "i18n_table_fr.h"
#include "i18n_table_de.h"
#include "i18n_table_it.h"
#include "i18n_table_pt.h"
#include "i18n_table_pl.h"
#include "i18n_table_nl.h"
#include "i18n_table_sv.h"
#include "i18n_table_uk.h"
#include "i18n_table_cs.h"
#include "i18n_table_ro.h"
#include "i18n_table_tr.h"
#include "i18n_table_vi.h"
#include "i18n_table_id.h"
#include "i18n_table_el.h"
#include "i18n_table_zh_cn.h"
#include "i18n_table_zh_tw.h"
#include "i18n_table_ja.h"
#include "i18n_table_ko.h"
#include "i18n_table_ar.h"
#include "i18n_table_he.h"
#include "i18n_table_hi.h"
#include "i18n_table_th.h"

struct i18n_table_ref { const struct i18n_pair *table; size_t len; };
#define TREF(name) { name, sizeof(name) / sizeof(name[0]) }

static const struct i18n_table_ref i18n_tables[I18N_LANG_LIMIT] = {
    [I18N_LANG_RU] = TREF(i18n_table_ru),
    [I18N_LANG_ES] = TREF(i18n_table_es),
    [I18N_LANG_FR] = TREF(i18n_table_fr),
    [I18N_LANG_DE] = TREF(i18n_table_de),
    [I18N_LANG_IT] = TREF(i18n_table_it),
    [I18N_LANG_PT] = TREF(i18n_table_pt),
    [I18N_LANG_PL] = TREF(i18n_table_pl),
    [I18N_LANG_NL] = TREF(i18n_table_nl),
    [I18N_LANG_SV] = TREF(i18n_table_sv),
    [I18N_LANG_UK] = TREF(i18n_table_uk),
    [I18N_LANG_CS] = TREF(i18n_table_cs),
    [I18N_LANG_RO] = TREF(i18n_table_ro),
    [I18N_LANG_TR] = TREF(i18n_table_tr),
    [I18N_LANG_VI] = TREF(i18n_table_vi),
    [I18N_LANG_ID] = TREF(i18n_table_id),
    [I18N_LANG_EL] = TREF(i18n_table_el),
    [I18N_LANG_ZH_CN] = TREF(i18n_table_zh_cn),
    [I18N_LANG_ZH_TW] = TREF(i18n_table_zh_tw),
    [I18N_LANG_JA] = TREF(i18n_table_ja),
    [I18N_LANG_KO] = TREF(i18n_table_ko),
    [I18N_LANG_AR] = TREF(i18n_table_ar),
    [I18N_LANG_HE] = TREF(i18n_table_he),
    [I18N_LANG_HI] = TREF(i18n_table_hi),
    [I18N_LANG_TH] = TREF(i18n_table_th),
};

/* Local patch: native display names for the Design/Window language
 * droplist (unix/config-gtk.c). Listed in full (all 25) regardless of
 * translation progress -- i18n_language_available() below is what
 * actually decides which of these the droplist offers, so a language
 * can be named here ready for whenever its po/xx.po is done without
 * prematurely exposing a half-translated UI. */
const struct i18n_lang_info i18n_languages[] = {
    { "English", I18N_LANG_EN },
    { "Русский", I18N_LANG_RU },
    { "Español", I18N_LANG_ES },
    { "Français", I18N_LANG_FR },
    { "Deutsch", I18N_LANG_DE },
    { "Italiano", I18N_LANG_IT },
    { "Português", I18N_LANG_PT },
    { "中文(简体)", I18N_LANG_ZH_CN },
    { "中文(繁體)", I18N_LANG_ZH_TW },
    { "日本語", I18N_LANG_JA },
    { "한국어", I18N_LANG_KO },
    { "العربية", I18N_LANG_AR },
    { "हिन्दी", I18N_LANG_HI },
    { "Türkçe", I18N_LANG_TR },
    { "Polski", I18N_LANG_PL },
    { "Nederlands", I18N_LANG_NL },
    { "Svenska", I18N_LANG_SV },
    { "Українська", I18N_LANG_UK },
    { "Tiếng Việt", I18N_LANG_VI },
    { "ไทย", I18N_LANG_TH },
    { "Bahasa Indonesia", I18N_LANG_ID },
    { "Čeština", I18N_LANG_CS },
    { "Ελληνικά", I18N_LANG_EL },
    { "עברית", I18N_LANG_HE },
    { "Română", I18N_LANG_RO },
};
const int i18n_languages_count =
    sizeof(i18n_languages) / sizeof(i18n_languages[0]);

bool i18n_language_available(int lang)
{
    if (lang == I18N_LANG_EN)
        return true;
    if (lang < 0 || lang >= I18N_LANG_LIMIT)
        return false;
    return i18n_tables[lang].table != NULL;
}

static int i18n_current_language = I18N_LANG_EN;

void i18n_set_language(int lang)
{
    if (!i18n_language_available(lang))
        lang = I18N_LANG_EN;
    i18n_current_language = lang;
}

int i18n_get_language(void)
{
    return i18n_current_language;
}

static int i18n_pair_cmp(const void *key, const void *elem)
{
    const struct i18n_pair *pair = (const struct i18n_pair *)elem;
    return strcmp((const char *)key, pair->msgid);
}

const char *i18n_translate(const char *s)
{
    const struct i18n_table_ref *ref;
    const struct i18n_pair *hit;

    if (!s || !*s)
        return s;
    if (i18n_current_language < 0 || i18n_current_language >= I18N_LANG_LIMIT)
        return s;

    ref = &i18n_tables[i18n_current_language];
    if (!ref->table)
        return s;      /* English, or a language not translated yet */

    /* The generated tables are sorted by msgid in byte order. */
    hit = (const struct i18n_pair *)bsearch(
        s, ref->table, ref->len, sizeof(struct i18n_pair), i18n_pair_cmp);

    return hit ? hit->msgstr : s;
}
