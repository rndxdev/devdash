#include "shell.h"
#include "theme.h"
#include "util.h"
#include <vte/vte.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VteTerminal *s_terminal;
static GPid s_child_pid;

static void on_child_exited(VteTerminal *term, gint status, gpointer data) {
    (void)status; (void)data;

    char *home = (char *)g_get_home_dir();
    char *shell_bin = (char *)g_getenv("SHELL");
    if (!shell_bin) shell_bin = "/bin/bash";

    char *argv[] = { shell_bin, NULL };
    char **envp = g_get_environ();
    envp = g_environ_setenv(envp, "HOME", home, TRUE);
    envp = g_environ_setenv(envp, "TERM", "xterm-256color", TRUE);

    vte_terminal_spawn_async(
        term,
        VTE_PTY_DEFAULT,
        home,
        argv,
        envp,
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL,
        -1,
        NULL,
        NULL, NULL);

    g_strfreev(envp);
}

GtkWidget *shell_create(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Title bar */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Shell</span>");
    gtk_box_pack_start(GTK_BOX(hbox), title, FALSE, FALSE, 0);

    /* Check which shell we're using */
    const char *shell_env = g_getenv("SHELL");
    const char *shell_name = shell_env ? strrchr(shell_env, '/') ? strrchr(shell_env, '/') + 1 : shell_env : "bash";

    char *sub = markup_fmt(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Running %s</span>", shell_name);
    gtk_box_pack_start(GTK_BOX(hbox), make_label(sub), FALSE, FALSE, 0);
    free(sub);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* VTE Terminal */
    GtkWidget *term = vte_terminal_new();
    s_terminal = VTE_TERMINAL(term);

    /* Catppuccin colors */
    GdkRGBA fg, bg;
    gdk_rgba_parse(&fg, CAT_TEXT);
    gdk_rgba_parse(&bg, CAT_CRUST);
    vte_terminal_set_color_foreground(s_terminal, &fg);
    vte_terminal_set_color_background(s_terminal, &bg);

    /* Catppuccin palette */
    GdkRGBA palette[16];
    const char *colors[] = {
        CAT_SURFACE1,  /* black */
        CAT_RED,       /* red */
        CAT_GREEN,     /* green */
        CAT_YELLOW,    /* yellow */
        CAT_BLUE,      /* blue */
        CAT_PINK,      /* magenta */
        CAT_TEAL,      /* cyan */
        CAT_SUBTEXT1,  /* white */
        CAT_SURFACE2,  /* bright black */
        CAT_RED,       /* bright red */
        CAT_GREEN,     /* bright green */
        CAT_YELLOW,    /* bright yellow */
        CAT_BLUE,      /* bright blue */
        CAT_PINK,      /* bright magenta */
        CAT_TEAL,      /* bright cyan */
        CAT_TEXT,      /* bright white */
    };
    for (int i = 0; i < 16; i++)
        gdk_rgba_parse(&palette[i], colors[i]);
    vte_terminal_set_colors(s_terminal, &fg, &bg, palette, 16);

    /* Prefer a Nerd Font so Powerline/oh-my-posh glyphs (Unicode PUA,
     * e.g. U+EBA9) render as icons instead of raw codepoints. Pango
     * accepts a comma-separated family list and falls through to the
     * next family when a glyph is missing. */
    vte_terminal_set_font(s_terminal,
        pango_font_description_from_string(
            "JetBrainsMono Nerd Font,"
            "FiraCode Nerd Font,"
            "MesloLGS Nerd Font,"
            "Hack Nerd Font,"
            "Symbols Nerd Font,"
            "Monospace 12"));
    vte_terminal_set_scrollback_lines(s_terminal, 10000);
    vte_terminal_set_cursor_blink_mode(s_terminal, VTE_CURSOR_BLINK_ON);
    vte_terminal_set_mouse_autohide(s_terminal, TRUE);

    /* Spawn shell */
    char *home = (char *)g_get_home_dir();
    char *shell_bin = NULL;

    shell_bin = (char *)g_getenv("SHELL");
    if (!shell_bin) shell_bin = "/bin/bash";

    char *argv[] = { shell_bin, NULL };

    /* Pass full environment so HOME, PATH, etc. are available */
    char **envp = g_get_environ();
    envp = g_environ_setenv(envp, "HOME", home, TRUE);
    envp = g_environ_setenv(envp, "TERM", "xterm-256color", TRUE);

    vte_terminal_spawn_async(
        s_terminal,
        VTE_PTY_DEFAULT,
        home,
        argv,
        envp,
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL,
        -1,
        NULL,
        NULL, NULL);

    g_strfreev(envp);

    g_signal_connect(term, "child-exited", G_CALLBACK(on_child_exited), NULL);

    GtkWidget *sw = make_scrolled(term);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

    return vbox;
}

void shell_refresh(void) {
    /* Terminal handles its own updates */
}

void shell_cleanup(void) {
    /* VTE cleans up on destroy */
}
