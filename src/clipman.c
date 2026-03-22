#include "clipman.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 100
#define PREVIEW_LEN 120

static char *s_entries[MAX_ENTRIES];
static int s_count;
static char *s_last_clip;
static GtkWidget *s_listbox;
static GtkWidget *s_search;
static GtkClipboard *s_clipboard;

static void add_entry(const char *text) {
    if (!text || !text[0]) return;

    /* Avoid duplicates at top */
    if (s_count > 0 && s_entries[0] && strcmp(s_entries[0], text) == 0) return;

    /* Shift everything down */
    if (s_count >= MAX_ENTRIES) {
        free(s_entries[MAX_ENTRIES - 1]);
        s_count = MAX_ENTRIES - 1;
    }
    for (int i = s_count; i > 0; i--)
        s_entries[i] = s_entries[i - 1];
    s_entries[0] = strdup(text);
    s_count++;
}

static void rebuild_list(void) {
    /* Clear */
    GList *children = gtk_container_get_children(GTK_CONTAINER(s_listbox));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(children);

    const char *filter = gtk_entry_get_text(GTK_ENTRY(s_search));
    int has_filter = filter && filter[0];

    for (int i = 0; i < s_count; i++) {
        if (has_filter && !strcasestr(s_entries[i], filter)) continue;

        /* Truncate preview */
        char preview[PREVIEW_LEN + 4];
        size_t len = strlen(s_entries[i]);
        if (len > PREVIEW_LEN) {
            memcpy(preview, s_entries[i], PREVIEW_LEN);
            strcpy(preview + PREVIEW_LEN, "...");
        } else {
            strcpy(preview, s_entries[i]);
        }

        /* Replace newlines for display */
        for (char *p = preview; *p; p++)
            if (*p == '\n') *p = ' ';

        char *escaped = g_markup_escape_text(preview, -1);
        char idx_str[8];
        snprintf(idx_str, sizeof(idx_str), "%d", i);

        char *m = markup_fmt(
            "<span font='10' foreground='" CAT_OVERLAY0 "'>#%s</span>  "
            "<span font='11' foreground='" CAT_TEXT "'>%s</span>",
            idx_str, escaped);

        GtkWidget *label = make_label(m);
        gtk_widget_set_margin_top(label, 4);
        gtk_widget_set_margin_bottom(label, 4);
        gtk_widget_set_margin_start(label, 8);
        gtk_list_box_insert(GTK_LIST_BOX(s_listbox), label, -1);

        free(m);
        g_free(escaped);
    }
    gtk_widget_show_all(s_listbox);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box; (void)data;
    int idx = gtk_list_box_row_get_index(row);

    /* Account for filtering */
    const char *filter = gtk_entry_get_text(GTK_ENTRY(s_search));
    int has_filter = filter && filter[0];
    int visible_idx = 0;
    for (int i = 0; i < s_count; i++) {
        if (has_filter && !strcasestr(s_entries[i], filter)) continue;
        if (visible_idx == idx) {
            gtk_clipboard_set_text(s_clipboard, s_entries[i], -1);
            /* Update last clip to avoid re-adding */
            free(s_last_clip);
            s_last_clip = strdup(s_entries[i]);
            return;
        }
        visible_idx++;
    }
}

static void on_search_changed(GtkWidget *entry, gpointer data) {
    (void)entry; (void)data;
    rebuild_list();
}

void clipman_refresh(void) {
    char *text = gtk_clipboard_wait_for_text(s_clipboard);
    if (!text) return;

    if (!s_last_clip || strcmp(s_last_clip, text) != 0) {
        free(s_last_clip);
        s_last_clip = strdup(text);
        add_entry(text);
        rebuild_list();
    }
    g_free(text);
}

GtkWidget *clipman_create(void) {
    s_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Clipboard Manager</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Click an entry to copy it back — stores last 100</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* Search */
    s_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_search), "Search clipboard history...");
    gtk_widget_set_name(s_search, "panel-search");
    g_signal_connect(s_search, "search-changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_search, FALSE, FALSE, 4);

    /* List */
    s_listbox = gtk_list_box_new();
    gtk_widget_set_name(s_listbox, "clip-list");
    g_signal_connect(s_listbox, "row-activated", G_CALLBACK(on_row_activated), NULL);

    GtkWidget *sw = make_scrolled(s_listbox);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    return vbox;
}

void clipman_cleanup(void) {
    for (int i = 0; i < s_count; i++) free(s_entries[i]);
    free(s_last_clip);
}
