#pragma once
#include <gtk/gtk.h>

/* Run a shell command and return malloc'd output. Caller must free. */
char *run_cmd(const char *cmd);

/* Strip trailing whitespace/newlines in-place. Returns s. */
char *trim_inplace(char *s);

/* Create a scrolled window wrapping child. */
GtkWidget *make_scrolled(GtkWidget *child);

/* Create a label with Pango markup. */
GtkWidget *make_label(const char *markup);

/* Convenience: format a markup label. Caller must free returned string. */
char *markup_fmt(const char *fmt, ...);
