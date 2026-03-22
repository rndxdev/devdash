#include "envman.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

enum { COL_ENV_KEY, COL_ENV_VAL, NUM_ENV_COLS };

static GtkListStore *s_store;
static GtkWidget *s_tree;
static GtkWidget *s_search;
static GtkClipboard *s_clipboard;

static void do_refresh(void) {
    gtk_list_store_clear(s_store);

    const char *filter = gtk_entry_get_text(GTK_ENTRY(s_search));
    int has_filter = filter && filter[0];

    for (char **env = environ; *env; env++) {
        char *eq = strchr(*env, '=');
        if (!eq) continue;

        char key[256];
        size_t klen = eq - *env;
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, *env, klen);
        key[klen] = '\0';

        const char *val = eq + 1;

        if (has_filter && !strcasestr(key, filter) && !strcasestr(val, filter))
            continue;

        GtkTreeIter iter;
        gtk_list_store_append(s_store, &iter);
        gtk_list_store_set(s_store, &iter,
            COL_ENV_KEY, key,
            COL_ENV_VAL, val,
            -1);
    }
}

static void on_search_changed(GtkWidget *entry, gpointer data) {
    (void)entry; (void)data;
    do_refresh();
}

static void on_copy_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    char *key = NULL, *val = NULL;
    gtk_tree_model_get(model, &iter, COL_ENV_KEY, &key, COL_ENV_VAL, &val, -1);
    if (key && val) {
        char *export_str = markup_fmt("export %s=\"%s\"", key, val);
        gtk_clipboard_set_text(s_clipboard, export_str, -1);
        free(export_str);
    }
    g_free(key);
    g_free(val);
}

GtkWidget *envman_create(void) {
    s_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Environment Manager</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>View env vars — select and copy as export</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* Search */
    s_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_search), "Filter variables...");
    gtk_widget_set_name(s_search, "panel-search");
    g_signal_connect(s_search, "search-changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_search, FALSE, FALSE, 4);

    /* TreeView */
    s_store = gtk_list_store_new(NUM_ENV_COLS, G_TYPE_STRING, G_TYPE_STRING);
    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    g_object_unref(s_store);
    gtk_widget_set_name(s_tree, "panel-tree");

    GtkCellRenderer *r1 = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c1 = gtk_tree_view_column_new_with_attributes(
        "Variable", r1, "text", COL_ENV_KEY, NULL);
    gtk_tree_view_column_set_resizable(c1, TRUE);
    gtk_tree_view_column_set_min_width(c1, 200);
    gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), c1);

    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    g_object_set(r2, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *c2 = gtk_tree_view_column_new_with_attributes(
        "Value", r2, "text", COL_ENV_VAL, NULL);
    gtk_tree_view_column_set_resizable(c2, TRUE);
    gtk_tree_view_column_set_expand(c2, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), c2);

    GtkWidget *sw = make_scrolled(s_tree);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *btn_copy = gtk_button_new_with_label("Copy as export");
    gtk_widget_set_name(btn_copy, "action-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_copy), "action-btn");
    g_signal_connect(btn_copy, "clicked", G_CALLBACK(on_copy_clicked), NULL);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    gtk_widget_set_name(btn_refresh, "action-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_refresh), "action-btn");
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(do_refresh), NULL);

    gtk_box_pack_start(GTK_BOX(hbox), btn_copy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    do_refresh();
    return vbox;
}

void envman_refresh(void) {}
void envman_cleanup(void) {}
