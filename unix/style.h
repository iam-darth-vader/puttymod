/*
 * Local addition: see style.c. Installs a Catppuccin Mocha CSS skin for
 * PuTTY's GTK dialogs, so they match the themed terminal window.
 */

#ifndef PUTTY_UNIX_STYLE_H
#define PUTTY_UNIX_STYLE_H

#include <gtk/gtk.h>
#include <stdbool.h>

/* Idempotent: safe to call more than once. */
void putty_apply_style(void);

#endif /* PUTTY_UNIX_STYLE_H */
