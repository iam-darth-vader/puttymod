/*
 * Minimal runtime interface localisation for the GTK front end.
 *
 * This is a local addition, not part of upstream PuTTY, which has no
 * internationalisation at all. Translations are held as generated C
 * tables (see po2c.py) rather than gettext .mo catalogues, so that the
 * language can be switched at runtime rather than only at process start.
 */

#ifndef PUTTY_UNIX_I18N_H
#define PUTTY_UNIX_I18N_H

/* Local patch: expanded from EN+RU to 25 languages. Order is fixed
 * once shipped (values are persisted as a plain int in
 * ~/.putty/global-appearance's UiLanguage key), so only ever APPEND
 * new languages before I18N_LANG_LIMIT -- never insert in the middle
 * or renumber, or existing users' saved language choice silently
 * changes to a different language. */
enum {
    I18N_LANG_EN = 0,
    I18N_LANG_RU,
    I18N_LANG_ES,
    I18N_LANG_FR,
    I18N_LANG_DE,
    I18N_LANG_IT,
    I18N_LANG_PT,
    I18N_LANG_ZH_CN,
    I18N_LANG_ZH_TW,
    I18N_LANG_JA,
    I18N_LANG_KO,
    I18N_LANG_AR,
    I18N_LANG_HI,
    I18N_LANG_TR,
    I18N_LANG_PL,
    I18N_LANG_NL,
    I18N_LANG_SV,
    I18N_LANG_UK,
    I18N_LANG_VI,
    I18N_LANG_TH,
    I18N_LANG_ID,
    I18N_LANG_CS,
    I18N_LANG_EL,
    I18N_LANG_HE,
    I18N_LANG_RO,
    I18N_LANG_LIMIT
};

/* Local patch: display name (in its OWN language, like any language
 * picker) plus the enum id, for the Design/Window language droplist
 * (unix/config-gtk.c) to iterate over without duplicating this list. */
struct i18n_lang_info { const char *name; int id; };
extern const struct i18n_lang_info i18n_languages[];
extern const int i18n_languages_count;

/* True for English (always) and for any language whose po/xx.po has
 * been translated and regenerated -- the Design/Window language
 * droplist uses this to only offer languages that actually work,
 * rather than exposing a half-translated UI for one still in
 * progress. */
bool i18n_language_available(int lang);

/* Select the active language. Unknown values fall back to English. */
void i18n_set_language(int lang);

int i18n_get_language(void);

/*
 * Translate a UI string. Returns the original pointer when there is no
 * translation (including for every string when the language is English),
 * so it is always safe to wrap a literal in this.
 */
const char *i18n_translate(const char *s);

/* Short alias, in the usual gettext spirit. */
#define I_(s) i18n_translate(s)

#endif /* PUTTY_UNIX_I18N_H */
