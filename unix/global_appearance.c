/*
 * global_appearance.c: local patch.
 *
 * A small profile-INDEPENDENT settings layer for the "Оформление"
 * (Appearance) config panel: font, ANSI/cursor colours, cursor style,
 * background transparency, and the config dialog's own accent/bg/fg
 * colours. These live in ~/.putty/global-appearance, a flat key=value
 * file, separate from any saved session -- they apply to every
 * connection regardless of which profile was used to make it.
 *
 * apply_global_appearance() overlays the stored values onto a Conf
 * that has just been loaded for a session, BEFORE it reaches the
 * config dialog, so the Terminal/Window panels and the new
 * "Оформление" panel always show (and use) the same, current, global
 * values rather than whatever that particular session file happened
 * to have saved for those fields.
 *
 * save_global_appearance() writes the current values of those same
 * Conf fields back out, called at the moment any session is actually
 * connected to (the same hook points used for distro auto-detect).
 *
 * apply_global_window_theme() is unrelated to Conf: it runs once at
 * startup, before any window exists, and installs a GtkCssProvider so
 * the dialog chrome itself doesn't depend on the system GTK theme.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gtk/gtk.h>

#include "putty.h"

static char *global_appearance_path(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    return dupprintf("%s/.putty/global-appearance", home);
}

bool global_appearance_exists(void)
{
    char *path = global_appearance_path();
    FILE *fp;

    if (!path)
        return true;   /* no $HOME: pretend it exists, don't touch anything */
    fp = fopen(path, "r");
    sfree(path);
    if (!fp)
        return false;
    fclose(fp);
    return true;
}

void apply_global_appearance(Conf *conf)
{
    char *path = global_appearance_path();
    FILE *fp;
    char line[512];

    if (!path)
        return;
    fp = fopen(path, "r");
    sfree(path);
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        char *eq;
        if (nl) *nl = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        {
            const char *key = line, *val = eq + 1;
            if (!strcmp(key, "Font")) {
                conf_set_fontspec(conf, CONF_font, fontspec_new(val));
            } else if (!strcmp(key, "WinFont")) {
                conf_set_fontspec(conf, CONF_win_font, fontspec_new(val));
            } else if (!strcmp(key, "UiLanguage")) {
                conf_set_int(conf, CONF_ui_language, atoi(val));
            } else if (!strcmp(key, "CurType")) {
                conf_set_int(conf, CONF_cursor_type, atoi(val));
            } else if (!strcmp(key, "BlinkCur")) {
                conf_set_bool(conf, CONF_blink_cur, atoi(val) != 0);
            } else if (!strcmp(key, "BgAlpha")) {
                conf_set_int(conf, CONF_bg_alpha, atoi(val));
            } else if (!strcmp(key, "WinAccent")) {
                conf_set_str(conf, CONF_win_accent, val);
            } else if (!strcmp(key, "WinBg")) {
                conf_set_str(conf, CONF_win_bg, val);
            } else if (!strcmp(key, "WinFg")) {
                conf_set_str(conf, CONF_win_fg, val);
            } else if (!strcmp(key, "WinAlpha")) {
                conf_set_int(conf, CONF_win_alpha, atoi(val));
            } else if (!strncmp(key, "Colour", 6)) {
                int idx = atoi(key + 6);
                int r, g, b;
                if (idx >= 0 && idx < 22 &&
                    sscanf(val, "%d,%d,%d", &r, &g, &b) == 3) {
                    conf_set_int_int(conf, CONF_colours, idx*3+0, r);
                    conf_set_int_int(conf, CONF_colours, idx*3+1, g);
                    conf_set_int_int(conf, CONF_colours, idx*3+2, b);
                }
            }
        }
    }
    fclose(fp);
}

void save_global_appearance(Conf *conf)
{
    char *path = global_appearance_path();
    char *dir;
    FILE *fp;
    int i;

    if (!path)
        return;

    dir = dupprintf("%s", path);
    {
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
    }
    mkdir(dir, 0700);
    sfree(dir);

    fp = fopen(path, "w");
    sfree(path);
    if (!fp)
        return;

    fprintf(fp, "Font=%s\n", conf_get_fontspec(conf, CONF_font)->name);
    fprintf(fp, "WinFont=%s\n", conf_get_fontspec(conf, CONF_win_font)->name);
    fprintf(fp, "UiLanguage=%d\n", conf_get_int(conf, CONF_ui_language));
    fprintf(fp, "CurType=%d\n", conf_get_int(conf, CONF_cursor_type));
    fprintf(fp, "BlinkCur=%d\n", conf_get_bool(conf, CONF_blink_cur) ? 1 : 0);
    fprintf(fp, "BgAlpha=%d\n", conf_get_int(conf, CONF_bg_alpha));
    fprintf(fp, "WinAccent=%s\n", conf_get_str(conf, CONF_win_accent));
    fprintf(fp, "WinBg=%s\n", conf_get_str(conf, CONF_win_bg));
    fprintf(fp, "WinFg=%s\n", conf_get_str(conf, CONF_win_fg));
    fprintf(fp, "WinAlpha=%d\n", conf_get_int(conf, CONF_win_alpha));
    for (i = 0; i < 22; i++) {
        fprintf(fp, "Colour%d=%d,%d,%d\n", i,
                conf_get_int_int(conf, CONF_colours, i*3+0),
                conf_get_int_int(conf, CONF_colours, i*3+1),
                conf_get_int_int(conf, CONF_colours, i*3+2));
    }
    fclose(fp);
}

/* Local patch: named custom colour-theme presets (Design/Themes "Save
 * as..."), see MAX_CUSTOM_THEMES / struct custom_theme in putty.h.
 * Stored in ~/.putty/custom-themes as one "Name=accent,bg,fg" line per
 * preset (each of accent/bg/fg a "#rrggbb" string) -- separate from
 * global-appearance since this is a *list* the user builds up over
 * time, not a single current value. */
static char *custom_themes_path(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    return dupprintf("%s/.putty/custom-themes", home);
}

int load_custom_themes(struct custom_theme *out, int max)
{
    char *path = custom_themes_path();
    FILE *fp;
    char line[256];
    int count = 0;

    if (!path)
        return 0;
    fp = fopen(path, "r");
    sfree(path);
    if (!fp)
        return 0;

    while (count < max && fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        char *eq, *c1, *c2;
        if (nl) *nl = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        {
            const char *name = line;
            char *rest = eq + 1;
            c1 = strchr(rest, ',');
            if (!c1) continue;
            *c1++ = '\0';
            c2 = strchr(c1, ',');
            if (!c2) continue;
            *c2++ = '\0';

            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, name, sizeof(out[count].name) - 1);
            strncpy(out[count].accent, rest, sizeof(out[count].accent) - 1);
            strncpy(out[count].bg, c1, sizeof(out[count].bg) - 1);
            strncpy(out[count].fg, c2, sizeof(out[count].fg) - 1);
            count++;
        }
    }
    fclose(fp);
    return count;
}

void save_custom_theme(const char *name, const char *accent,
                       const char *bg, const char *fg)
{
    struct custom_theme existing[MAX_CUSTOM_THEMES];
    int n = load_custom_themes(existing, MAX_CUSTOM_THEMES);
    char *path = custom_themes_path();
    char *dir;
    FILE *fp;
    int i;
    bool replaced = false;

    if (!path)
        return;

    dir = dupprintf("%s", path);
    {
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
    }
    mkdir(dir, 0700);
    sfree(dir);

    fp = fopen(path, "w");
    sfree(path);
    if (!fp)
        return;

    for (i = 0; i < n; i++) {
        if (!strcmp(existing[i].name, name)) {
            fprintf(fp, "%s=%s,%s,%s\n", name, accent, bg, fg);
            replaced = true;
        } else {
            fprintf(fp, "%s=%s,%s,%s\n", existing[i].name,
                    existing[i].accent, existing[i].bg, existing[i].fg);
        }
    }
    if (!replaced)
        fprintf(fp, "%s=%s,%s,%s\n", name, accent, bg, fg);

    fclose(fp);
}

/* Read just the window-theme keys, without needing a Conf at all --
 * used at startup, before any Conf/session has been loaded. Falls
 * back to the CONF_win_* compiled-in defaults (see conf.h) if the
 * file or a given key doesn't exist yet. */
static void read_window_theme_hex(char **accent, char **bg, char **fg,
                                  int *alpha, char **font)
{
    char *path = global_appearance_path();
    FILE *fp;
    char line[512];

    *accent = dupstr("3DAEE9");
    *bg = dupstr("1E1E20");
    *fg = dupstr("EFF0F1");
    *alpha = 92;
    *font = dupstr("Sans 10");

    if (!path)
        return;
    fp = fopen(path, "r");
    sfree(path);
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        char *eq;
        if (nl) *nl = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        {
            const char *key = line, *val = eq + 1;
            if (!strcmp(key, "WinAccent")) {
                sfree(*accent); *accent = dupstr(val);
            } else if (!strcmp(key, "WinBg")) {
                sfree(*bg); *bg = dupstr(val);
            } else if (!strcmp(key, "WinFg")) {
                sfree(*fg); *fg = dupstr(val);
            } else if (!strcmp(key, "WinAlpha")) {
                *alpha = atoi(val);
            } else if (!strcmp(key, "WinFont") && *val) {
                sfree(*font); *font = dupstr(val);
            }
        }
    }
    fclose(fp);
}

/*
 * Local patch: everything here is scoped to the ".putty-dialog" CSS
 * class (added to each window in our_dialog_new(), unix/utils/
 * our_dialog.c) rather than bare "window"/"dialog" element selectors.
 * Those bare selectors match *any* toplevel GtkWindow, including the
 * terminal's -- which has its own independent cairo-driven theming
 * and transparency (CONF_bg_alpha, see unix/window.c). A screen-wide
 * provider with an unscoped "window {...}" rule silently paints an
 * opaque CSS background under the terminal too, defeating its RGBA
 * visual regardless of connect order, since GDK resolves CSS
 * background *before* the terminal's own "draw" handler ever runs.
 * Scoping to a class the terminal never gets avoids that entirely,
 * rather than relying on provider add-order (fragile: see also the
 * PRIORITY_USER choice below, for the same reason relative to the
 * static Catppuccin skin in style.c, which is scoped the same way).
 */
void apply_global_window_theme(void)
{
    char *accent, *bg, *fg, *font;
    int alpha;
    char *css, *fontcss, *alphastr;
    GtkCssProvider *provider;
    const char *fontname;

    read_window_theme_hex(&accent, &bg, &fg, &alpha, &font);

    /* font may carry a "client:"/"server:" prefix, same convention as
     * the terminal's own CONF_font (see appearance_preview_draw() in
     * unix/dialog.c) -- meaningless for a CSS font-family, so strip
     * it the same way before handing off to Pango. */
    fontname = font;
    if (!strncmp(fontname, "client:", 7) || !strncmp(fontname, "server:", 7))
        fontname += 7;
    {
        PangoFontDescription *desc = pango_font_description_from_string(
            fontname);
        const char *family = pango_font_description_get_family(desc);
        int size = pango_font_description_get_size(desc) / PANGO_SCALE;
        fontcss = dupprintf(
            ".putty-dialog { font-family: \"%s\"; font-size: %dpt; }\n",
            family && *family ? family : "Sans", size > 0 ? size : 10);
        pango_font_description_free(desc);
    }

    /* alpha as "0.NN" built from integer arithmetic, not %g/%f: those
     * are locale-sensitive, and under a locale that uses ',' for the
     * decimal separator (e.g. ru_RU) "alpha(#hex, 0,92)" is a CSS
     * parse error (extra comma inside the function's argument list),
     * silently dropping this whole provider -- exactly what broke the
     * dialog's transparency the first time this shipped. */
    {
        int clamped = alpha < 0 ? 0 : alpha > 100 ? 100 : alpha;
        alphastr = dupprintf("0.%02d", clamped);
    }

    css = dupprintf(
        "%s"
        ".putty-dialog {"
        " background-color: alpha(#%s, %s); color: #%s; }\n"
        ".putty-dialog treeview {"
        " background-color: #%s; color: #%s; }\n"
        ".putty-dialog treeview:selected, .putty-dialog row:selected {"
        " background-color: #%s; color: #%s; }\n"
        ".putty-dialog button {"
        " background-color: shade(#%s, 1.15); color: #%s;"
        " border-color: #%s; }\n"
        ".putty-dialog button:hover { border-color: #%s; }\n"
        ".putty-dialog entry {"
        " background-color: shade(#%s, 1.15); color: #%s;"
        " border-color: #%s; }\n"
        ".putty-dialog entry:focus { border-color: #%s; }\n"
        ".putty-dialog check, .putty-dialog radio {"
        " background-color: shade(#%s, 1.15); }\n"
        ".putty-dialog check:checked, .putty-dialog radio:checked {"
        " background-color: #%s; }\n",
        fontcss,
        bg, alphastr, fg,               /* window/dialog */
        bg, fg,                         /* treeview */
        accent, bg,                     /* selected row */
        bg, fg, accent,                 /* button */
        accent,                         /* button:hover */
        bg, fg, accent,                 /* entry */
        accent,                         /* entry:focus */
        bg,                             /* check/radio */
        accent                          /* checked */
        );

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    /* PRIORITY_USER: GTK's own convention for "user customisation that
     * should win over an application/theme's own CSS" -- used here so
     * these user-chosen colours reliably beat style.c's static
     * Catppuccin skin (also PRIORITY_APPLICATION) regardless of which
     * provider happened to be added to the screen first. */
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);

    sfree(css);
    sfree(fontcss);
    sfree(alphastr);
    sfree(accent);
    sfree(bg);
    sfree(fg);
    sfree(font);
}
