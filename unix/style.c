/*
 * style.c - local addition: a Catppuccin Mocha skin for PuTTY's GTK
 * dialogs, applied as an application-priority CSS provider.
 *
 * Upstream PuTTY just inherits whatever GTK theme is in force. That
 * looks out of place next to our themed terminal window, so we install
 * our own provider once, at the point the config box is first built.
 */

#include "style.h"

#if GTK_CHECK_VERSION(3,0,0)

static const char putty_css[] =
    /* ---- palette (Catppuccin Mocha) ----------------------------------
     * base #1e1e2e   mantle #181825   surface0 #313244
     * surface1 #45475a surface2 #585b70 text #cdd6f4
     * subtext0 #a6adc8 blue #89b4fa    red  #f38ba8
     */
    /* Local patch: scoped to .putty-dialog (set on every dialog-family
     * window in our_dialog_new(), unix/utils/our_dialog.c) instead of
     * bare "window, dialog, .background" -- that also matched the
     * terminal's own toplevel (every GtkWindow gets the internal
     * ".background" style class) and painted an opaque CSS background
     * under it, silently defeating its independent RGBA transparency
     * regardless of draw order. See the longer comment in
     * apply_global_window_theme(), unix/global_appearance.c, which
     * layers user-chosen colours over this at PRIORITY_USER. */
    ".putty-dialog {"
    "  background-color: #1e1e2e;"
    "  color: #cdd6f4;"
    "}"

    ".putty-dialog label { color: #cdd6f4; }"

    /* Local patch: "No internet connection" warning next to Open/
     * Cancel (config.c's ssd->netwarning, unix/dialog.c's
     * dlg_set_netwarning_visible()). Styled as a small solid badge
     * (not just coloured text) so it actually catches the eye next to
     * two ordinary buttons -- needs the "label" in the selector for
     * enough specificity to beat the plain ".putty-dialog label" rule
     * just above. */
    ".putty-dialog label.putty-netwarning {"
    "  color: #1e1e2e;"
    "  font-weight: bold;"
    "  background-color: #f38ba8;"
    "  border-radius: 6px;"
    "  padding: 4px 10px;"
    "}"

    /* the category tree down the left-hand side */
    "treeview.view {"
    "  background-color: #181825;"
    "  color: #cdd6f4;"
    "  border-radius: 8px;"
    "}"
    "treeview.view:selected {"
    "  background-color: #89b4fa;"
    "  color: #1e1e2e;"
    "}"
    "treeview.view:hover {"
    "  background-color: #313244;"
    "}"

    /* text entry fields */
    "entry {"
    "  background-color: #313244;"
    "  color: #cdd6f4;"
    "  border: 1px solid #45475a;"
    "  border-radius: 6px;"
    "  padding: 4px 8px;"
    "  caret-color: #f5e0dc;"
    "}"
    "entry:focus {"
    "  border-color: #89b4fa;"
    "  background-color: #1e1e2e;"
    "}"
    "entry:disabled {"
    "  color: #6c7086;"
    "  background-color: #262637;"
    "}"

    /* push buttons */
    "button {"
    "  background-image: none;"
    "  background-color: #45475a;"
    "  color: #cdd6f4;"
    "  border: 1px solid #585b70;"
    "  border-radius: 6px;"
    "  padding: 4px 12px;"
    "}"
    "button:hover {"
    "  background-color: #585b70;"
    "  border-color: #89b4fa;"
    "}"
    "button:active, button:checked {"
    "  background-color: #89b4fa;"
    "  color: #1e1e2e;"
    "}"
    "button:disabled {"
    "  background-color: #262637;"
    "  color: #6c7086;"
    "}"

    /* check boxes and radio buttons */
    "check, radio {"
    "  background-color: #313244;"
    "  border: 1px solid #585b70;"
    "}"
    "check:checked, radio:checked {"
    "  background-color: #89b4fa;"
    "  border-color: #89b4fa;"
    "  color: #1e1e2e;"
    "}"

    /* drop-down lists and their popups */
    "combobox button { border-radius: 6px; }"
    "menu, popover, .popup {"
    "  background-color: #181825;"
    "  color: #cdd6f4;"
    "  border: 1px solid #45475a;"
    "  border-radius: 8px;"
    "}"
    "menuitem:hover { background-color: #89b4fa; color: #1e1e2e; }"

    /* list boxes (e.g. the saved-sessions list, cipher order) */
    "list, list row {"
    "  background-color: #181825;"
    "  color: #cdd6f4;"
    "}"
    "list row:selected { background-color: #89b4fa; color: #1e1e2e; }"

    /* notebook pages carrying each settings panel */
    "notebook, notebook > stack { background-color: #1e1e2e; }"
    "notebook header { background-color: #181825; }"

    /* slim scrollbars */
    "scrollbar { background-color: #181825; border: none; }"
    "scrollbar slider {"
    "  background-color: #45475a;"
    "  border-radius: 10px;"
    "  min-width: 8px;"
    "  min-height: 8px;"
    "}"
    "scrollbar slider:hover { background-color: #585b70; }"

    /* separators and frames */
    "separator { background-color: #45475a; }"
    "frame border { border-color: #45475a; border-radius: 8px; }"

    /* tooltips */
    "tooltip {"
    "  background-color: #181825;"
    "  color: #cdd6f4;"
    "  border: 1px solid #45475a;"
    "  border-radius: 6px;"
    "}";

void putty_apply_style(void)
{
    static bool applied = false;
    GtkCssProvider *provider;
    GdkScreen *screen;

    if (applied)
        return;
    applied = true;

    screen = gdk_screen_get_default();
    if (!screen)
        return;

    provider = gtk_css_provider_new();
    /* Errors here are non-fatal: worst case we keep the stock theme. */
    if (gtk_css_provider_load_from_data(provider, putty_css, -1, NULL)) {
        gtk_style_context_add_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

#else /* !GTK 3 */

void putty_apply_style(void) { }

#endif
