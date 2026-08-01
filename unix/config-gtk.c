/*
 * config-gtk.c - the GTK-specific parts of the PuTTY configuration
 * box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"
#include "i18n.h"

static void about_handler(dlgcontrol *ctrl, dlgparam *dlg,
                          void *data, int event)
{
    if (event == EVENT_ACTION) {
        about_box(ctrl->context.p);
    }
}

/* Local patch: Design/Window's language droplist. A plain droplist
 * (not ctrl_radiobuttons, which was fine for 2 languages but would be
 * an unusable wall of 25 buttons) populated from i18n_languages[]
 * (unix/i18n.h), filtered to i18n_language_available() so a language
 * whose po/xx.po isn't translated yet simply doesn't appear as an
 * option -- same "not silently offering a broken half-translated UI"
 * reasoning as everywhere else this codebase falls back to English on
 * unrecognised/missing text. */
static void language_handler(dlgcontrol *ctrl, dlgparam *dlg,
                             void *data, int event)
{
    Conf *conf = (Conf *)data;
    int i;

    if (event == EVENT_REFRESH) {
        int oldlang = conf_get_int(conf, CONF_ui_language);
        int selrow = 0, row = 0;
        dlg_update_start(ctrl, dlg);
        dlg_listbox_clear(ctrl, dlg);
        for (i = 0; i < i18n_languages_count; i++) {
            if (!i18n_language_available(i18n_languages[i].id))
                continue;
            dlg_listbox_addwithid(ctrl, dlg, i18n_languages[i].name,
                                  i18n_languages[i].id);
            if (i18n_languages[i].id == oldlang)
                selrow = row;
            row++;
        }
        dlg_listbox_select(ctrl, dlg, selrow);
        dlg_update_done(ctrl, dlg);
        conf_set_int(conf, CONF_ui_language, oldlang);        /* restore */
    } else if (event == EVENT_SELCHANGE) {
        int idx = dlg_listbox_index(ctrl, dlg);
        if (idx >= 0)
            conf_set_int(conf, CONF_ui_language,
                        dlg_listbox_getid(ctrl, dlg, idx));
    }
}

/* Local patch: context for the "Apply & Restart" button beside the
 * language droplist -- restart_self_with_new_language() (unix/
 * platform.h, implemented per-frontend in main-gtk-*.c) needs both the
 * dialog's top-level window (to parent a confirmation box) and whether
 * this is a midsession "Change Settings" box (to know whether that
 * confirmation is needed at all), neither of which the ordinary `data'
 * argument (the session Conf*) carries. */
struct language_apply_data {
    void *win;
    bool midsession;
};

static void language_apply_handler(dlgcontrol *ctrl, dlgparam *dlg,
                                    void *data, int event)
{
    struct language_apply_data *lad =
        (struct language_apply_data *)ctrl->context.p;
    Conf *conf = (Conf *)data;

    if (event == EVENT_ACTION)
        restart_self_with_new_language(lad->win, lad->midsession, conf);
}

void gtk_setup_config_box(struct controlbox *b, bool midsession, void *win)
{
    struct controlset *s, *s2;
    dlgcontrol *c;
    int i;

    if (!midsession) {
        /*
         * Add the About button to the standard panel.
         */
        s = ctrl_getset(b, "", "", "");
        c = ctrl_pushbutton(s, "About", 'a', HELPCTX(no_help),
                            about_handler, P(win));
        c->column = 0;
    }

    /*
     * Interface language. Local addition: upstream PuTTY has no
     * internationalisation at all, so this drives our own translation
     * table (see unix/i18n.c) rather than gettext. Its own panel
     * (Design/Language, titled from config.c alongside the other
     * Design/* panels) rather than the per-profile Window/Appearance
     * panel, since the language you read the UI in obviously isn't a
     * per-session thing -- and its own panel rather than sharing
     * Design/Window, since it has nothing to do with window colours.
     */
    s = ctrl_getset(b, "Design/Language", "language",
                    "Interface language");
    ctrl_columns(s, 2, 70, 30);
    {
        dlgcontrol *dl = ctrl_droplist(s, "Language:", NO_SHORTCUT, 40,
                                       HELPCTX(no_help),
                                       language_handler, P(NULL));
        dl->column = 0;

        struct language_apply_data *lad = (struct language_apply_data *)
            ctrl_alloc(b, sizeof(struct language_apply_data));
        lad->win = win;
        lad->midsession = midsession;
        dlgcontrol *ab = ctrl_pushbutton(s, "Apply & Restart", NO_SHORTCUT,
                                         HELPCTX(no_help),
                                         language_apply_handler, P(lad));
        ab->column = 1;
    }
    ctrl_columns(s, 1, 100);
    {
        /* Local patch: translate the template before substituting
         * `appname', same reasoning as config.c's appname-titled
         * panels -- see the comment on config.c's i18n.h include. */
        char *str = dupprintf(
            I_("Apply & Restart saves your choice and restarts %s"
               " immediately. Just closing this dialog without pressing"
               " it applies the change next time %s starts."),
            appname, appname);
        ctrl_text(s, str, HELPCTX(no_help));
        sfree(str);
    }

    /*
     * GTK makes it rather easier to put the scrollbar on the left
     * than Windows does!
     */
    s = ctrl_getset(b, "Window", "scrollback",
                    "Control the scrollback in the window");
    ctrl_checkbox(s, "Scrollbar on left", 'l',
                  HELPCTX(no_help),
                  conf_checkbox_handler,
                  I(CONF_scrollbar_on_left));
    /*
     * Really this wants to go just after `Display scrollbar'. See
     * if we can find that control, and do some shuffling.
     */
    for (i = 0; i < s->ncontrols; i++) {
        c = s->ctrls[i];
        if (c->type == CTRL_CHECKBOX &&
            c->context.i == CONF_scrollbar) {
            /*
             * Control i is the scrollbar checkbox.
             * Control s->ncontrols-1 is the scrollbar-on-left one.
             */
            if (i < s->ncontrols-2) {
                c = s->ctrls[s->ncontrols-1];
                memmove(s->ctrls+i+2, s->ctrls+i+1,
                        (s->ncontrols-i-2)*sizeof(dlgcontrol *));
                s->ctrls[i+1] = c;
            }
            break;
        }
    }

    /*
     * X requires three more fonts: bold, wide, and wide-bold; also
     * we need the fiddly shadow-bold-offset control. This would
     * make the Window/Appearance panel rather unwieldy and large,
     * so I think the sensible thing here is to _move_ this
     * controlset into a separate Window/Fonts panel!
     */
    s2 = ctrl_getset(b, "Window/Appearance", "font",
                     "Font settings");
    /* Remove this controlset from b. */
    for (i = 0; i < b->nctrlsets; i++) {
        if (b->ctrlsets[i] == s2) {
            memmove(b->ctrlsets+i, b->ctrlsets+i+1,
                    (b->nctrlsets-i-1) * sizeof(*b->ctrlsets));
            b->nctrlsets--;
            ctrl_free_set(s2);
            break;
        }
    }
    /*
     * Local patch: the ordinary-text font selector that used to be
     * here (bound to CONF_font, "Font used for ordinary text") is now
     * profile-independent and edited exclusively from Design/Terminal.
     * The CJK wide-text font below has no equivalent there, so it
     * stays; this panel just no longer duplicates the main font pick.
     */
    ctrl_settitle(b, "Window/Fonts", "Options controlling font usage");
    s = ctrl_getset(b, "Window/Fonts", "font",
                    "Fonts for displaying non-bold text");
    ctrl_fontsel(s, "Font used for wide (CJK) text", 'w',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_widefont));
    s = ctrl_getset(b, "Window/Fonts", "fontbold",
                    "Fonts for displaying bolded text");
    ctrl_fontsel(s, "Font used for bolded text", 'b',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_boldfont));
    ctrl_fontsel(s, "Font used for bold wide text", 'i',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_wideboldfont));
    ctrl_checkbox(s, "Use shadow bold instead of bold fonts", 'u',
                  HELPCTX(no_help),
                  conf_checkbox_handler,
                  I(CONF_shadowbold));
    ctrl_text(s, "(Note that bold fonts or shadow bolding are only"
              " used if you have not requested bolding to be done by"
              " changing the text colour.)",
              HELPCTX(no_help));
    ctrl_editbox(s, "Horizontal offset for shadow bold:", 'z', 20,
                 HELPCTX(no_help), conf_editbox_handler,
                 I(CONF_shadowboldoffset), ED_INT);

    /*
     * Markus Kuhn feels, not totally unreasonably, that it's good
     * for all applications to shift into UTF-8 mode if they notice
     * that they've been started with a LANG setting dictating it,
     * so that people don't have to keep remembering a separate
     * UTF-8 option for every application they use. Therefore,
     * here's an override option in the Translation panel.
     */
    s = ctrl_getset(b, "Window/Translation", "trans",
                    "Character set translation on received data");
    ctrl_checkbox(s, "Override with UTF-8 if locale says so", 'l',
                  HELPCTX(translation_utf8_override),
                  conf_checkbox_handler,
                  I(CONF_utf8_override));

#ifdef OSX_META_KEY_CONFIG
    /*
     * On OS X, there are multiple reasonable opinions about whether
     * Option or Command (or both, or neither) should act as a Meta
     * key, or whether they should have their normal OS functions.
     */
    s = ctrl_getset(b, "Terminal/Keyboard", "meta",
                    "Choose the Meta key:");
    ctrl_checkbox(s, "Option key acts as Meta", 'p',
                  HELPCTX(no_help),
                  conf_checkbox_handler, I(CONF_osx_option_meta));
    ctrl_checkbox(s, "Command key acts as Meta", 'm',
                  HELPCTX(no_help),
                  conf_checkbox_handler, I(CONF_osx_command_meta));
#endif

    if (!midsession) {
        /*
         * Allow the user to specify the window class as part of the saved
         * configuration, so that they can have their window manager treat
         * different kinds of PuTTY and pterm differently if they want to.
         */
        s = ctrl_getset(b, "Window/Behaviour", "x11",
                        "X Window System settings");
        ctrl_editbox(s, "Window class name:", 'z', 50,
                     HELPCTX(no_help), conf_editbox_handler,
                     I(CONF_winclass), ED_STR);
    }
}
