#include "procmon.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

enum {
    COL_PM_PID, COL_PM_USER, COL_PM_CPU, COL_PM_MEM, COL_PM_CMD,
    COL_PM_PID_INT, NUM_PM_COLS
};

static GtkListStore *s_store;
static GtkWidget *s_tree;
static GtkWidget *s_search;

static void do_refresh(void) {
    char *out = run_cmd("ps aux --sort=-%cpu 2>/dev/null");
    if (!out) return;

    gtk_list_store_clear(s_store);

    const char *filter = gtk_entry_get_text(GTK_ENTRY(s_search));
    int has_filter = filter && filter[0];

    char *line = out;
    /* skip header */
    line = strchr(line, '\n');
    if (!line) { free(out); return; }
    line++;

    int count = 0;
    while (*line && count < 100) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        char user[64] = "", cpu[16] = "", mem[16] = "", cmd[512] = "";
        int pid = 0;
        /* USER PID %CPU %MEM VSZ RSS TTY STAT START TIME COMMAND */
        int n = sscanf(line, "%63s %d %15s %15s %*s %*s %*s %*s %*s %*s %511[^\n]",
                       user, &pid, cpu, mem, cmd);

        if (n >= 5) {
            if (!has_filter || strcasestr(cmd, filter) || strcasestr(user, filter)) {
                char pid_str[16];
                snprintf(pid_str, sizeof(pid_str), "%d", pid);

                GtkTreeIter iter;
                gtk_list_store_append(s_store, &iter);
                gtk_list_store_set(s_store, &iter,
                    COL_PM_PID, pid_str,
                    COL_PM_USER, user,
                    COL_PM_CPU, cpu,
                    COL_PM_MEM, mem,
                    COL_PM_CMD, cmd,
                    COL_PM_PID_INT, pid,
                    -1);
                count++;
            }
        }

        if (!eol) break;
        line = eol + 1;
    }
    free(out);
}

static void on_kill_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int pid = 0;
    gtk_tree_model_get(model, &iter, COL_PM_PID_INT, &pid, -1);
    if (pid <= 0) return;

    char msg[128];
    snprintf(msg, sizeof(msg), "Kill process %d?", pid);
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", msg);
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (resp == GTK_RESPONSE_YES) {
        kill(pid, SIGTERM);
        g_timeout_add(500, (GSourceFunc)do_refresh, NULL);
    }
}

static void on_search_changed(GtkWidget *entry, gpointer data) {
    (void)entry; (void)data;
    do_refresh();
}

GtkWidget *procmon_create(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Process Monitor</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    /* Search */
    s_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_search), "Filter processes...");
    gtk_widget_set_name(s_search, "panel-search");
    g_signal_connect(s_search, "search-changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_search, FALSE, FALSE, 4);

    /* TreeView */
    s_store = gtk_list_store_new(NUM_PM_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    g_object_unref(s_store);
    gtk_widget_set_name(s_tree, "panel-tree");

    const char *titles[] = {"PID", "User", "CPU%", "MEM%", "Command"};
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_expand(c, i == 4);
        gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), c);
    }

    GtkWidget *sw = make_scrolled(s_tree);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    gtk_widget_set_name(btn_refresh, "action-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_refresh), "action-btn");
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(do_refresh), NULL);

    GtkWidget *btn_kill = gtk_button_new_with_label("Kill");
    gtk_widget_set_name(btn_kill, "danger-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_kill), "danger-btn");
    g_signal_connect(btn_kill, "clicked", G_CALLBACK(on_kill_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_kill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    do_refresh();
    return vbox;
}

void procmon_refresh(void) { do_refresh(); }
void procmon_cleanup(void) {}
