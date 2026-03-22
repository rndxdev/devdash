#include "sshdash.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_HOSTS 64

typedef struct {
    char alias[128];
    char hostname[256];
    char user[64];
    char port[8];
    char identity[256];
} SshHost;

enum {
    COL_SSH_ALIAS, COL_SSH_HOST, COL_SSH_USER, COL_SSH_PORT,
    COL_SSH_IDX, NUM_SSH_COLS
};

static SshHost s_hosts[MAX_HOSTS];
static int s_nhosts;
static GtkListStore *s_store;
static GtkWidget *s_tree;

static void parse_ssh_config(void) {
    const char *home = g_get_home_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/.ssh/config", home);

    FILE *f = fopen(path, "r");
    if (!f) return;

    s_nhosts = 0;
    int cur = -1;
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;

        /* Find key and value */
        char *sp = line;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        if (!*sp) continue;
        *sp = '\0';
        char *val = sp + 1;
        while (*val && isspace((unsigned char)*val)) val++;
        trim_inplace(val);

        if (strcasecmp(line, "Host") == 0) {
            if (s_nhosts >= MAX_HOSTS) break;
            /* Skip wildcard entries */
            if (strchr(val, '*') || strchr(val, '?')) continue;
            cur = s_nhosts++;
            memset(&s_hosts[cur], 0, sizeof(SshHost));
            strncpy(s_hosts[cur].alias, val, sizeof(s_hosts[0].alias) - 1);
            strcpy(s_hosts[cur].port, "22");
        } else if (cur >= 0) {
            if (strcasecmp(line, "HostName") == 0 || strcasecmp(line, "Hostname") == 0)
                strncpy(s_hosts[cur].hostname, val, sizeof(s_hosts[0].hostname) - 1);
            else if (strcasecmp(line, "User") == 0)
                strncpy(s_hosts[cur].user, val, sizeof(s_hosts[0].user) - 1);
            else if (strcasecmp(line, "Port") == 0)
                strncpy(s_hosts[cur].port, val, sizeof(s_hosts[0].port) - 1);
            else if (strcasecmp(line, "IdentityFile") == 0)
                strncpy(s_hosts[cur].identity, val, sizeof(s_hosts[0].identity) - 1);
        }
    }
    fclose(f);
}

static void do_refresh(void) {
    parse_ssh_config();
    gtk_list_store_clear(s_store);

    for (int i = 0; i < s_nhosts; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(s_store, &iter);
        gtk_list_store_set(s_store, &iter,
            COL_SSH_ALIAS, s_hosts[i].alias,
            COL_SSH_HOST, s_hosts[i].hostname[0] ? s_hosts[i].hostname : s_hosts[i].alias,
            COL_SSH_USER, s_hosts[i].user[0] ? s_hosts[i].user : "-",
            COL_SSH_PORT, s_hosts[i].port,
            COL_SSH_IDX, i,
            -1);
    }
}

static void on_connect_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int idx = 0;
    gtk_tree_model_get(model, &iter, COL_SSH_IDX, &idx, -1);
    if (idx < 0 || idx >= s_nhosts) return;

    char cmd[600];
    snprintf(cmd, sizeof(cmd),
        "x-terminal-emulator -e ssh %s 2>/dev/null || "
        "gnome-terminal -- ssh %s 2>/dev/null || "
        "xterm -e ssh %s 2>/dev/null &",
        s_hosts[idx].alias, s_hosts[idx].alias, s_hosts[idx].alias);
    g_spawn_command_line_async(cmd, NULL);
}

static void on_edit_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    const char *home = g_get_home_dir();
    char cmd[600];
    snprintf(cmd, sizeof(cmd),
        "x-terminal-emulator -e ${EDITOR:-nano} %s/.ssh/config 2>/dev/null || "
        "gnome-terminal -- ${EDITOR:-nano} %s/.ssh/config 2>/dev/null &",
        home, home);
    g_spawn_command_line_async(cmd, NULL);
}

GtkWidget *sshdash_create(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>SSH Dashboard</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Parsed from ~/.ssh/config — select and connect</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* TreeView */
    s_store = gtk_list_store_new(NUM_SSH_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    g_object_unref(s_store);
    gtk_widget_set_name(s_tree, "panel-tree");

    const char *titles[] = {"Host", "Hostname", "User", "Port"};
    for (int i = 0; i < 4; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_expand(c, i == 1);
        gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), c);
    }

    GtkWidget *sw = make_scrolled(s_tree);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *btn_connect = gtk_button_new_with_label("Connect");
    gtk_widget_set_name(btn_connect, "action-btn");
    g_signal_connect(btn_connect, "clicked", G_CALLBACK(on_connect_clicked), NULL);

    GtkWidget *btn_edit = gtk_button_new_with_label("Edit Config");
    gtk_widget_set_name(btn_edit, "action-btn");
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_edit_clicked), NULL);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    gtk_widget_set_name(btn_refresh, "action-btn");
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(do_refresh), NULL);

    gtk_box_pack_start(GTK_BOX(hbox), btn_connect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_edit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    do_refresh();
    return vbox;
}

void sshdash_refresh(void) {}
void sshdash_cleanup(void) {}
